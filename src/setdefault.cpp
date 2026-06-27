#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// IApplicationAssociationRegistration UUID
// {4E530B0A-E611-4C77-A3AC-9031D022281B}
static const GUID CLSID_ApplicationAssociationRegistration =
    { 0x4E530B0A, 0xE611, 0x4C77, { 0xA3, 0xAC, 0x90, 0x31, 0xD0, 0x22, 0x28, 0x1B } };
// IID_IApplicationAssociationRegistration
static const GUID IID_IApplicationAssociationRegistration =
    { 0x4E530B0A, 0xE611, 0x4C77, { 0xA3, 0xAC, 0x90, 0x31, 0xD0, 0x22, 0x28, 0x1B } };

int wmain(int argc, WCHAR *argv[])
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IApplicationAssociationRegistration *pAAR = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr,
        CLSCTX_INPROC_SERVER, IID_IApplicationAssociationRegistration, (void**)&pAAR);

    if (FAILED(hr))
    {
        wprintf(L"CoCreateInstance failed: 0x%08X\n", hr);
        CoUninitialize();
        return 1;
    }

    // Register our app for .svg extension
    LPCWSTR appPath = L"C:\\Program Files\\SVG-NATIVE\\SvgViewer.exe";
    LPCWSTR progId = L"SvgViewer.svg";

    // Create progid if it doesn't exist
    HKEY hKey;
    WCHAR keyPath[512];
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\%s", progId);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SVG Image", 18);
        RegCloseKey(hKey);

        swprintf_s(keyPath, L"SOFTWARE\\Classes\\%s\\shell\\open\\command", progId);
        if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
            nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        {
            WCHAR cmd[512];
            swprintf_s(cmd, L"\"%s\" \"%%1\"", appPath);
            RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
            RegCloseKey(hKey);
        }
    }

    // Set as default for .svg
    hr = pAAR->SetAppAsDefault(L"SvgViewer.svg", L".svg", AT_FILEEXTENSION);
    wprintf(L"SetAppAsDefault(.svg): 0x%08X\n", hr);

    // Also set for image/svg+xml MIME
    hr = pAAR->SetAppAsDefault(L"SvgViewer.svg", L"image/svg+xml", AT_MIMETYPE);
    wprintf(L"SetAppAsDefault(MIME): 0x%08X\n", hr);

    pAAR->Release();
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
