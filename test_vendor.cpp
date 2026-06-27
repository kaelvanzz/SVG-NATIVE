#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

const GUID GUID_Vendor_SVG = 
    { 0xf0e749ca, 0xeded, 0x4589, { 0xa7, 0x3a, 0xee, 0x0e, 0x62, 0x6a, 0x2a, 0x2b } };

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("CoInit: 0x%08X\n", hr);

    IWICImagingFactory *pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    printf("Factory: 0x%08X\n", hr);

    // Try with vendor GUID matching
    IWICBitmapDecoder *pDec = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        &GUID_Vendor_SVG, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("CreateDecoder (vendor=%d): 0x%08X\n", SUCCEEDED(hr) ? 1 : 0, hr);

    // Try with GUID_NULL vendor (should match all)
    GUID nullGUID = GUID_NULL;
    IWICBitmapDecoder *pDec2 = nullptr;
    hr = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        &nullGUID, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec2);
    printf("CreateDecoder (null vendor): 0x%08X\n", hr);

    // Show WIC version
    IWICComponentInfo *pInfo = nullptr;
    hr = pFactory->CreateComponentInfo(CLSID_WICImagingFactory, &pInfo);
    if (SUCCEEDED(hr))
    {
        WCHAR ver[32] = {};
        UINT len = 0;
        pInfo->GetVersion(32, ver, &len);
        printf("WIC version: %S\n", ver);
        pInfo->Release();
    }

    pFactory->Release();
    CoUninitialize();
    return 0;
}
