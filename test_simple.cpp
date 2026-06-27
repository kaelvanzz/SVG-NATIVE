#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

const CLSID CLSID_SvgWicDecoder = { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

int main()
{
    printf("Start\n");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("CoInit: 0x%08X\n", hr);

    IWICImagingFactory *pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    printf("Factory: 0x%08X\n", hr);

    IWICBitmapDecoderInfo *pInfo = nullptr;
    hr = pFactory->CreateComponentInfo(CLSID_SvgWicDecoder, (IWICComponentInfo**)&pInfo);
    printf("Info: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        // Check patterns
        UINT cnt = 0;
        hr = pInfo->GetPatterns(0, nullptr, &cnt, nullptr);
        printf("Patterns count: %u (hr=0x%08X)\n", cnt, hr);

        // Write a simple SVG and try from stream
        const char *svg = "<svg xmlns='http://www.w3.org/2000/svg'><circle cx='50' cy='50' r='40' fill='red'/></svg>";
        IStream *pStream = nullptr;
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
        printf("CreateStream: 0x%08X\n", hr);
        if (SUCCEEDED(hr))
        {
            ULONG written = 0;
            pStream->Write(svg, (ULONG)strlen(svg), &written);
            LARGE_INTEGER zz = {};
            pStream->Seek(zz, STREAM_SEEK_SET, nullptr);

            BOOL matched = FALSE;
            hr = pInfo->MatchesPattern(pStream, &matched);
            printf("MatchesPattern: 0x%08X (matched=%d)\n", hr, (int)matched);

            // Try CreateDecoderFromStream
            pStream->Seek(zz, STREAM_SEEK_SET, nullptr);
            IWICBitmapDecoder *pDec = nullptr;
            hr = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDec);
            printf("CreateDecoderFromStream: 0x%08X\n", hr);

            pStream->Release();
        }
        pInfo->Release();
    }
    pFactory->Release();
    CoUninitialize();
    printf("Done\n");
    return 0;
}
