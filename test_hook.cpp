#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // First test WITHOUT hook: SVG should fail
    IWICImagingFactory *pFactory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

    IWICBitmapDecoder *pDec = nullptr;
    HRESULT hr1 = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("Without hook - SVG: 0x%08X\n", hr1);
    if (pDec) pDec->Release();

    pFactory->Release();

    // Now load hook and try again
    HMODULE hHook = LoadLibraryW(L"C:\\Users\\user\\Documents\\SVG-NATIVE\\bin\\x64\\svghook.dll");
    printf("LoadLibrary: %p\n", hHook);
    if (!hHook) { CoUninitialize(); return 1; }

    auto InstallHooks = (void(*)())GetProcAddress(hHook, "InstallHooks");
    InstallHooks();
    Sleep(3000); // generous wait

    pFactory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

    pDec = nullptr;
    HRESULT hr2 = pFactory->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("With hook - SVG: 0x%08X\n", hr2);

    if (SUCCEEDED(hr2) && pDec)
    {
        UINT w = 0, h = 0;
        IWICBitmapFrameDecode *pFrame = nullptr;
        if (SUCCEEDED(pDec->GetFrame(0, &pFrame)))
        {
            pFrame->GetSize(&w, &h);
            printf("  Size: %ux%u\n", w, h);
            pFrame->Release();
        }
        pDec->Release();
    }

    pFactory->Release();
    FreeLibrary(hHook);
    CoUninitialize();
    printf("Done\n");
    return 0;
}
