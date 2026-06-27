#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <shlwapi.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { printf("CoCreateInstance factory failed: 0x%08X\n", hr); return 1; }

    // Test CreateDecoderFromFilename
    IWICBitmapDecoder *pDec = nullptr;
    hr = pFactory->CreateDecoderFromFilename(L"test.svg", nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("CreateDecoderFromFilename(.svg): 0x%08X\n", hr);

    // Test CreateDecoderFromStream
    IStream *pStream = nullptr;
    hr = SHCreateStreamOnFileW(L"test.svg", STGM_READ, &pStream);
    if (SUCCEEDED(hr))
    {
        IWICBitmapDecoder *pDec2 = nullptr;
        hr = pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDec2);
        printf("CreateDecoderFromStream(.svg): 0x%08X\n", hr);
        if (SUCCEEDED(hr))
        {
            UINT w, h;
            pDec2->GetFrame(0, (IWICBitmapFrameDecode**)&pDec2);
            IWICBitmapFrameDecode *pFrame = nullptr;
            hr = pDec2->GetFrame(0, &pFrame);
            if (SUCCEEDED(hr))
            {
                pFrame->GetSize(&w, &h);
                printf("  Frame size: %u x %u\n", w, h);
                pFrame->Release();
            }
            pDec2->Release();
        }
        pStream->Release();
    }
    else
    {
        printf("SHCreateStreamOnFileW failed: 0x%08X\n", hr);
    }

    // Test CreateComponentEnumerator
    IEnumUnknown *pEnum = nullptr;
    hr = pFactory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &pEnum);
    if (SUCCEEDED(hr))
    {
        ULONG fetched = 0;
        IUnknown *pUnk = nullptr;
        int count = 0;
        while (pEnum->Next(1, &pUnk, &fetched) == S_OK)
        {
            IWICBitmapDecoderInfo *pInfo = nullptr;
            if (SUCCEEDED(pUnk->QueryInterface(IID_PPV_ARGS(&pInfo))))
            {
                WCHAR name[256] = {};
                UINT len = 0;
                pInfo->GetFriendlyName(256, name, &len);
                printf("  Decoder %d: %S\n", ++count, name);
                pInfo->Release();
            }
            pUnk->Release();
        }
        pEnum->Release();
        printf("Total decoders: %d\n", count);
    }

    pFactory->Release();
    CoUninitialize();
    return 0;
}
