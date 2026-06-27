#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

// PNG decoder CLSID from wincodec.h
// {E018945B-AA86-4008-9FBD-7BF0B3AEB5D5}
static const CLSID CLSID_WICPNGDecoder = 
    { 0xE018945B, 0xAA86, 0x4008, { 0x9F, 0xBD, 0x7B, 0xF0, 0xB3, 0xAE, 0xB5, 0xD5 } };

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pF = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF));

    // Test 1: Can we CoCreateInstance the PNG decoder directly?
    IWICBitmapDecoder *pDec = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICPNGDecoder, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDec));
    printf("CoCreateInstance(PNG decoder): 0x%08X\n", hr);
    if (pDec) pDec->Release();

    // Test 2: CreateDecoderFromFilename with .png
    pDec = nullptr;
    hr = pF->CreateDecoderFromFilename(
        L"C:\\Windows\\Web\\Screen\\img101.jpg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("CreateDecoderFromFilename(.jpg): 0x%08X\n", hr);
    if (pDec) pDec->Release();

    // Test 3: CreateDecoderFromFilename with .svg
    pDec = nullptr;
    hr = pF->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("CreateDecoderFromFilename(.svg): 0x%08X\n", hr);
    if (pDec) pDec->Release();

    // Test 4: Enumerate decoders, find PNG decoder CLSID
    IEnumUnknown *pEnum = nullptr;
    hr = pF->CreateComponentEnumerator(0x1, 0, &pEnum);
    if (SUCCEEDED(hr))
    {
        IUnknown *pUnk = nullptr;
        ULONG fetched = 0;
        while (pEnum->Next(1, &pUnk, &fetched) == S_OK)
        {
            IWICBitmapDecoderInfo *pInfo = nullptr;
            if (SUCCEEDED(pUnk->QueryInterface(IID_PPV_ARGS(&pInfo))))
            {
                CLSID clsid;
                pInfo->GetCLSID(&clsid);
                if (clsid == CLSID_WICPNGDecoder)
                    {
                        WCHAR name[128] = {};
                        pInfo->GetFriendlyName(128, name, nullptr);
                        wprintf(L"  Found PNG: %s\n", name);
                    }
                pInfo->Release();
            }
            pUnk->Release();
        }
        pEnum->Release();
    }

    pF->Release();
    CoUninitialize();
    return 0;
}
