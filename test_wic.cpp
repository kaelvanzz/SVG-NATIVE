#include <windows.h>
#include <wincodec.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

const CLSID CLSID_SvgWicDecoder =
    { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { printf("CoInitialize failed: 0x%08X\n", hr); return 1; }

    IWICImagingFactory *pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { printf("WIC factory: 0x%08X\n", hr); CoUninitialize(); return 1; }
    printf("WIC factory OK\n");

    // 1. Try to find our decoder by container format
    IWICComponentInfo *pCompInfo = nullptr;
    GUID svgFmt = { 0xfec14e3f, 0x427a, 0x4736, { 0xaa, 0xe6, 0x27, 0xed, 0x84, 0xf6, 0x93, 0x22 } };
    hr = pFactory->CreateComponentInfo(CLSID_SvgWicDecoder, &pCompInfo);
    printf("CreateComponentInfo(our CLSID): 0x%08X\n", hr);
    if (SUCCEEDED(hr)) pCompInfo->Release();

    // 2. Enumerate all WIC decoders
    IEnumUnknown *pEnum = nullptr;
    hr = pFactory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &pEnum);
    if (SUCCEEDED(hr))
    {
        ULONG count = 0;
        pEnum->Reset();
        // First get count
        pEnum->Next(0, nullptr, &count);
        pEnum->Reset();
        printf("Total decoders: %u\n", count);
        ULONG fetched = 0;
        IUnknown *pUnk = nullptr;
        while (pEnum->Next(1, &pUnk, &fetched) == S_OK && fetched > 0)
        {
            IWICBitmapDecoderInfo *pInfo = nullptr;
            if (SUCCEEDED(pUnk->QueryInterface(IID_PPV_ARGS(&pInfo))))
            {
                GUID fmtGUID = {};
                pInfo->GetContainerFormat(&fmtGUID);
                WCHAR name[128] = {};
                UINT len = 0;
                pInfo->GetFriendlyName(128, name, &len);
                printf("  Decoder: %S (fmt: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X})\n",
                    name,
                    fmtGUID.Data1, fmtGUID.Data2, fmtGUID.Data3,
                    fmtGUID.Data4[0], fmtGUID.Data4[1], fmtGUID.Data4[2], fmtGUID.Data4[3],
                    fmtGUID.Data4[4], fmtGUID.Data4[5], fmtGUID.Data4[6], fmtGUID.Data4[7]);
                pInfo->Release();
            }
            pUnk->Release();
            fetched = 0;
        }
        pEnum->Release();
    }
    else
        printf("Enum decoders failed: 0x%08X\n", hr);

    // 3. Try CreateDecoderFromFilename with default vendor matching
    IWICBitmapDecoder *pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    printf("CreateDecoder (default): 0x%08X\n", hr);
    if (SUCCEEDED(hr))
    {
        WCHAR name[128] = {};
        GUID fmtGUID = {};
        pDecoder->GetContainerFormat(&fmtGUID);
        printf("Decoder: {%08X-%04X-%04X-...}\n", fmtGUID.Data1, fmtGUID.Data2, fmtGUID.Data3);
        pDecoder->Release();
    }

    // 4. Try opening file as stream with our decoder
    HANDLE hFile = CreateFileW(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        IStream *pStream = nullptr;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
        if (SUCCEEDED(hr))
        {
            char buf[65536];
            DWORD read = 0;
            while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
                pStream->Write(buf, read, nullptr);
            SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);

            IWICBitmapDecoder *pDec2 = nullptr;
            hr = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDec2);
            printf("CreateDecoderFromStream: 0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                printf("Stream decoder OK\n");
                pDec2->Release();
            }
            pStream->Release();
        }
        CloseHandle(hFile);
    }

    pFactory->Release();
    CoUninitialize();
    return 0;
}
