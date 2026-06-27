#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

#define MY_WICComponentDecoder 0x1

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pF = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF));

    const GUID CLSID_SvgWicDecoder =
        { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

    // Enumerate all WIC decoders
    IEnumUnknown *pEnum = nullptr;
    HRESULT hr = pF->CreateComponentEnumerator(MY_WICComponentDecoder,
        WICComponentEnumerateDefault, &pEnum);
    printf("CreateComponentEnumerator: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        IUnknown *pUnk = nullptr;
        ULONG fetched = 0;
        int count = 0;
        BOOL found = FALSE;
        while (pEnum->Next(1, &pUnk, &fetched) == S_OK && fetched > 0)
        {
            IWICBitmapDecoderInfo *pInfo = nullptr;
            if (SUCCEEDED(pUnk->QueryInterface(IID_PPV_ARGS(&pInfo))))
            {
                WCHAR name[128] = {};
                UINT len = 0;
                pInfo->GetFriendlyName(128, name, &len);
                CLSID clsid;
                pInfo->GetCLSID(&clsid);
                printf("  [%d] %ls (CLSID=%08X-%04X-%04X)\n",
                    count++, name, clsid.Data1, clsid.Data2, clsid.Data3);

                if (clsid == CLSID_SvgWicDecoder)
                {
                    found = TRUE;
                    // Check container format
                    GUID fmt;
                    pInfo->GetContainerFormat(&fmt);
                    printf("        Container: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
                        fmt.Data1, fmt.Data2, fmt.Data3,
                        fmt.Data4[0], fmt.Data4[1],
                        fmt.Data4[2], fmt.Data4[3], fmt.Data4[4],
                        fmt.Data4[5], fmt.Data4[6], fmt.Data4[7]);

                    // Check FileExtensions
                    WCHAR exts[256] = {};
                    UINT extLen = 0;
                    pInfo->GetFileExtensions(256, exts, &extLen);
                    printf("        Extensions: %ls\n", exts);
                }
                pInfo->Release();
            }
            pUnk->Release();
        }
        printf("\nFound our decoder: %s\n", found ? "YES" : "NO");
        pEnum->Release();
    }

    pF->Release();
    CoUninitialize();
    return 0;
}
