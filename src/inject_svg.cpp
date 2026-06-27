#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>

static void FindProcessThreads(DWORD pid, DWORD **threads, int *count)
{
    *threads = nullptr;
    *count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    DWORD cap = 0;
    if (Thread32First(hSnap, &te))
    {
        do {
            if (te.th32OwnerProcessID == pid)
            {
                if (*count >= cap)
                {
                    cap = cap ? cap * 2 : 32;
                    DWORD *tmp = (DWORD*)realloc(*threads, cap * sizeof(DWORD));
                    if (!tmp) { free(*threads); *threads = nullptr; *count = 0; CloseHandle(hSnap); return; }
                    *threads = tmp;
                }
                (*threads)[(*count)++] = te.th32ThreadID;
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
}

static DWORD FindProcessByName(LPCWSTR name)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(hSnap, &pe))
    {
        do {
            WCHAR *p = pe.szExeFile;
            while (*p) { if (*p >= L'A' && *p <= L'Z') *p += 32; p++; }
            if (wcsstr(pe.szExeFile, name))
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

int wmain(int argc, WCHAR *argv[])
{
    wprintf(L"SVG WIC Hook Injector\n");
    wprintf(L"Usage: inject_svg.exe [process_name]\n");
    wprintf(L"Default: Photos.exe\n\n");

    LPCWSTR targetName = (argc >= 2) ? argv[1] : L"photos.exe";

    DWORD pid = FindProcessByName(targetName);
    if (!pid)
    {
        wprintf(L"ERROR: %s not found (make sure it's running)\n", targetName);
        return 1;
    }
    wprintf(L"Found PID=%u\n", pid);

    DWORD *threads = nullptr;
    int nThreads = 0;
    FindProcessThreads(pid, &threads, &nThreads);
    wprintf(L"Found %d thread(s)\n", nThreads);
    if (nThreads == 0) { free(threads); return 1; }

    // Find svghook.dll
    WCHAR dllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    WCHAR *pBack = wcsrchr(dllPath, L'\\');
    if (pBack) *pBack = 0;
    StringCchPrintfW(dllPath, MAX_PATH, L"%s\\svghook.dll", dllPath);
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        StringCchPrintfW(dllPath, MAX_PATH, L"C:\\Program Files\\SVG-NATIVE\\svghook.dll");
        if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES)
        {
            wprintf(L"ERROR: svghook.dll not found\n");
            free(threads); return 1;
        }
    }
    wprintf(L"DLL: %s\n", dllPath);

    HMODULE hHookDll = LoadLibraryW(dllPath);
    if (!hHookDll) { wprintf(L"ERROR: LoadLibrary failed (%u)\n", GetLastError()); free(threads); return 1; }

    HOOKPROC hookProc = (HOOKPROC)GetProcAddress(hHookDll, "CbtHookProc");
    if (!hookProc) { wprintf(L"ERROR: CbtHookProc not exported\n"); FreeLibrary(hHookDll); free(threads); return 1; }

    // Reset the ready event
    HANDLE hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SVG_WIC_Hook_Ready");
    if (hReadyEvent) ResetEvent(hReadyEvent);

    // Inject into each thread (try all, but one success is enough)
    int injected = 0;
    for (int i = 0; i < nThreads; i++)
    {
        HHOOK hook = SetWindowsHookExW(WH_CBT, hookProc, hHookDll, threads[i]);
        if (hook)
        {
            UnhookWindowsHookEx(hook);
            injected++;
            if (injected == 1)
                wprintf(L"  Injected into thread %u\n", threads[i]);
        }
    }
    wprintf(L"Injected into %d thread(s)\n", injected);

    if (injected == 0)
    {
        wprintf(L"SetWindowsHookEx failed - trying alternative injection...\n");
        // Try a few specific threads with a different approach
        // Sometimes UWP processes need special handling
        wprintf(L"Trying WH_GETMESSAGE hook...\n");
        for (int i = 0; i < min(nThreads, 5); i++)
        {
            HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, hookProc, hHookDll, threads[i]);
            if (hook) { UnhookWindowsHookEx(hook); injected++; wprintf(L"  Injected (WH_GETMESSAGE) into thread %u\n", threads[i]); break; }
        }
    }

    if (injected > 0)
    {
        wprintf(L"Waiting for hook init... ");
        if (hReadyEvent && WaitForSingleObject(hReadyEvent, 15000) == WAIT_OBJECT_0)
            wprintf(L"OK - hooks active\n");
        else
            wprintf(L"TIMEOUT (hooks may not be active)\n");
    }
    else
    {
        wprintf(L"FAILED - no injection method worked\n");
        DWORD err = GetLastError();
        wprintf(L"Last error: %u (0x%08X)\n", err, err);
    }

    if (hReadyEvent) CloseHandle(hReadyEvent);
    FreeLibrary(hHookDll);
    free(threads);
    return injected > 0 ? 0 : 1;
}
