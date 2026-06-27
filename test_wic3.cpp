#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

const CLSID CLSID_SvgWicDecoder =
    { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pFactory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

    // 1. Get our decoder info  
    IWICBitmapDecoderInfo *pInfo = nullptr;
    hr = pFactory->CreateComponentInfo(CLSID_SvgWicDecoder, (IWICComponentInfo**)&pInfo);
    printf("Get decoder info: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        // 2. Check patterns
        UINT count = 0;
        hr = pInfo->GetPatterns(0, nullptr, &count, nullptr);
        printf("Pattern count: %u (hr=0x%08X)\n", count, hr);

        // 3. Try matching patterns against a file  
        IStream *pStream = nullptr;
        hr = SHCreateStreamOnFileW(
            L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
            STGM_READ, &pStream);
        printf("Open stream: 0x%08X\n", hr);

        if (SUCCEEDED(hr))
        {
            WICBitmapPattern patterns[16] = {};
            BOOL matched = FALSE;
            hr = pInfo->MatchesPattern(pStream, &matched);
            printf("MatchesPattern: 0x%08X (matched=%u)\n", hr, matched);

            // 4. Try querying capability directly
            LARGE_INTEGER z = {};
            pStream->Seek(z, STREAM_SEEK_SET, nullptr);
            DWORD cap = 0;
            // Create our own decoder and query
            IWICBitmapDecoder *pDec = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_SvgWicDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDec))))
            {
                pStream->Seek(z, STREAM_SEEK_SET, nullptr);
                hr = pDec->QueryCapability(pStream, &cap);
                printf("Direct QueryCapability: 0x%08X (cap=0x%X)\n", hr, cap);
                pDec->Release();
            }

            pStream->Release();
        }

        pInfo->Release();
    }

    // 5. Try CreateDecoderFromFilename for a file that DOES start with <svg
    // Write a minimal SVG to temp that starts directly with <svg
    IWICBitmapDecoder *pDec2 = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec2);
    printf("CreateDecoderFromFilename: 0x%08X\n", hr);

    // 6. Try with a DIFFERENT SVG that starts with <svg directly
    // Write test SVG
    const char *svgSimple = "<svg xmlns='http://www.w3.org/2000/svg'><circle cx='50' cy='50' r='40' fill='red'/></svg>";
    HANDLE hTemp = CreateFileW(L"C:\\Users\\user\\Documents\\SVG-NATIVE\\test_tiny.svg",
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hTemp != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hTemp, svgSimple, (DWORD)strlen(svgSimple), &written, nullptr);
        CloseHandle(hTemp);
    }

    IWICBitmapDecoder *pDec3 = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Documents\\SVG-NATIVE\\test_tiny.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec3);
    printf("CreateDecoderFromFilename (simple): 0x%08X\n", hr);
    if (SUCCEEDED(hr))
    {
        UINT w = 0, h = 0;
        IWICBitmapFrameDecode *pFrame = nullptr;
        pDec3->GetFrame(0, &pFrame);
        pFrame->GetSize(&w, &h);
        printf("Simple SVG size: %u x %u\n", w, h);
        pFrame->Release();
        pDec3->Release();
    }

    // 7. Try CreateDecoderFromStream  
    {
        IStream *pStr = nullptr;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStr);
        if (SUCCEEDED(hr))
        {
            pStr->Write(svgSimple, (ULONG)strlen(svgSimple), nullptr);
            LARGE_INTEGER zz = {};
            pStr->Seek(zz, STREAM_SEEK_SET, nullptr);

            IWICBitmapDecoder *pDec4 = nullptr;
            hr = pFactory->CreateDecoderFromStream(pStr, nullptr, WICDecodeMetadataCacheOnDemand, &pDec4);
            printf("CreateDecoderFromStream (simple): 0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                UINT w = 0, h = 0;
                IWICBitmapFrameDecode *pFrame = nullptr;
                pDec4->GetFrame(0, &pFrame);
                pFrame->GetSize(&w, &h);
                printf("Stream SVG size: %u x %u\n", w, h);
                pFrame->Release();
                pDec4->Release();
            }
            pStr->Release();
        }
    }

    pFactory->Release();
    CoUninitialize();
    return 0;
}
