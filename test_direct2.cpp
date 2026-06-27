#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

const CLSID CLSID_SvgWicDecoder = { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };
const GUID GUID_ContainerFormatSvg = { 0xa6ba1b82, 0x2489, 0x4b33, { 0x9f, 0x3a, 0xca, 0x6b, 0x5c, 0x3a, 0x9a, 0x4b } };

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pF = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF));

    // Approach 1: Create our decoder directly, then use CreateFastMetadataEncoderFromDecoder
    // This is just to see if WIC will accept our decoder

    IWICBitmapDecoder *pOurDec = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SvgWicDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pOurDec));
    printf("Our decoder: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        // Open SVG file as stream
        HANDLE hFile = CreateFileW(
            L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        IStream *pStream = nullptr;
        CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
        char buf[65536]; DWORD read = 0;
        while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
            pStream->Write(buf, read, nullptr);
        CloseHandle(hFile);
        LARGE_INTEGER z = {};
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);

        hr = pOurDec->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
        printf("Initialize: 0x%08X\n", hr);

        // Try WIC-specific operations
        IWICBitmapSource *pSrc = nullptr;
        IWICBitmapFrameDecode *pFrame = nullptr;
        pOurDec->GetFrame(0, &pFrame);
        if (pFrame)
        {
            UINT w = 0, h = 0;
            pFrame->GetSize(&w, &h);
            printf("Frame: %u x %u\n", w, h);

            // Approach 2: CreateScaledBitmapSource from frame
            IWICBitmapScaler *pScaler = nullptr;
            hr = pF->CreateBitmapScaler(&pScaler);
            printf("CreateScaler: 0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                hr = pScaler->Initialize(pFrame, 256, 256, WICBitmapInterpolationModeFant);
                printf("Scale to 256: 0x%08X\n", hr);
                pScaler->Release();
            }
            pFrame->Release();
        }

        pStream->Release();
        pOurDec->Release();
    }

    // Approach 3: See what happens when we CreateDecoderFromFilename with specific vendor
    IWICBitmapDecoder *pDec = nullptr;
    hr = pF->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
    printf("CreateDecoderFromFilename: 0x%08X\n", hr);

    // Approach 4: Use WIC's CreateDecoderFromFilename with JPEG XL decoder
    // to see if WIC can find JPEG XL decoder (confirms WIC works at all)
    IWICBitmapDecoder *pJxlDec = nullptr;
    hr = pF->CreateDecoderFromFilename(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pJxlDec);
    printf("CreateDecoder (nonexistent): 0x%08X\n", hr);
    
    // Try with a .jpg file to confirm WIC works
    IWICBitmapDecoder *pJpgDec = nullptr;
    hr = pF->CreateDecoderFromFilename(
        L"C:\\Windows\\Web\\Screen\\img101.jpg",
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pJpgDec);
    printf("CreateDecoder (.jpg): 0x%08X\n", hr);
    if (SUCCEEDED(hr))
    {
        printf("  JPEG decoder OK!\n");
        pJpgDec->Release();
    }

    pF->Release();
    CoUninitialize();
    return 0;
}
