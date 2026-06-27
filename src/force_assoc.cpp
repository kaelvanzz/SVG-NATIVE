#include <windows.h>
#include <aclapi.h>
#include <stdio.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

// Delete a protected registry key by taking ownership first
static HRESULT ForceDeleteKey(HKEY hRoot, const WCHAR *subKey)
{
    HKEY hKey;
    LONG l = RegOpenKeyExW(hRoot, subKey, 0, WRITE_OWNER | READ_CONTROL | DELETE, &hKey);
    if (l != ERROR_SUCCESS) return HRESULT_FROM_WIN32(l);

    // Take ownership (admin sid)
    PSID pAdminSid = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &pAdminSid);
    if (pAdminSid)
    {
        SetSecurityInfo(hKey, SE_REGISTRY_KEY, OWNER_SECURITY_INFORMATION, pAdminSid, nullptr, nullptr, nullptr);

        // Give full control to admins
        EXPLICIT_ACCESSW ea;
        ZeroMemory(&ea, sizeof(ea));
        ea.grfAccessPermissions = KEY_ALL_ACCESS;
        ea.grfAccessMode = SET_ACCESS;
        ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        BuildTrusteeWithSidW(&ea.Trustee, pAdminSid);
        PACL pNewDacl = nullptr;
        if (ERROR_SUCCESS == SetEntriesInAclW(1, &ea, nullptr, &pNewDacl))
        {
            SetSecurityInfo(hKey, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, pNewDacl, nullptr);
            LocalFree(pNewDacl);
        }
        FreeSid(pAdminSid);
    }

    RegCloseKey(hKey);

    // Now delete it
    l = RegDeleteKeyExW(hRoot, subKey, KEY_WOW64_64KEY, 0);
    if (l != ERROR_SUCCESS) l = RegDeleteTreeW(hRoot, subKey);
    return HRESULT_FROM_WIN32(l);
}

static HRESULT SetupAssoc()
{
    HKEY hKey;
    WCHAR keyPath[512];

    // Create SvgViewer.svg ProgId
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\SvgViewer.svg");
    if (FAILED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        return E_FAIL;
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SVG Image", 18);
    RegCloseKey(hKey);

    // Create open command
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\SvgViewer.svg\\shell\\open\\command");
    if (FAILED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        return E_FAIL;
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
        (BYTE*)L"\"C:\\Program Files\\SVG-NATIVE\\SvgViewer.exe\" \"%1\"",
        (DWORD)((wcslen(L"\"C:\\Program Files\\SVG-NATIVE\\SvgViewer.exe\" \"%1\"") + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);

    // Set .svg default ProgId
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\.svg");
    if (FAILED(HRESULT_FROM_WIN32(RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
        KEY_WRITE, &hKey))))
        return E_FAIL;
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SvgViewer.svg", (DWORD)((wcslen(L"SvgViewer.svg") + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);

    // Remove Chrome from OpenWithProgids
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\.svg\\OpenWithProgids");
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0,
        KEY_WRITE, &hKey))))
    {
        RegDeleteValueW(hKey, L"ChromeHTML");
        RegDeleteValueW(hKey, L"MSEdgeHTM");
        RegDeleteValueW(hKey, L"svgfile");
        RegCloseKey(hKey);
    }

    return S_OK;
}

int wmain()
{
    HRESULT hr = SetupAssoc();
    wprintf(L"SetupAssoc: 0x%08X\n", hr);

    // Delete UserChoice (takes ownership first)
    hr = ForceDeleteKey(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.svg\\UserChoice");
    wprintf(L"ForceDelete UserChoice: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        // Also delete OpenWithList to force fresh enumeration
        ForceDeleteKey(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.svg\\OpenWithList");
    }

    wprintf(L"Done. Please restart Explorer for changes to take effect.\n");
    return SUCCEEDED(hr) ? 0 : 1;
}
