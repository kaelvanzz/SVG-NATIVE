#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

const CLSID CLSID_SvgWicDecoder =
    { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };
static const GUID MY_VENDOR =
    { 0xF0E749CA, 0xEDEF, 0x4589, { 0xA7, 0x3A, 0xEE, 0x0E, 0x62, 0x6A, 0x2A, 0x2B } };

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IWICImagingFactory *pF = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF));
    printf("Factory: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        LPCWSTR path = L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg";

        // 1. Try CreateDecoderFromFilename with vendor GUID
        IWICBitmapDecoder *pDec = nullptr;
        hr = pF->CreateDecoderFromFilename(
            path, &GUID_VendorMicrosoft, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &pDec);
        printf("CreateDecoderFromFilename (with vendor): 0x%08X\n", hr);
        if (pDec) pDec->Release();

        // 2. Create a stream and try CreateDecoderFromStream
        HANDLE hFile2 = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        IStream *pStream = nullptr;
        if (hFile2 != INVALID_HANDLE_VALUE)
        {
            CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
            char buf[262144]; DWORD read = 0;
            while (ReadFile(hFile2, buf, sizeof(buf), &read, nullptr) && read > 0)
                pStream->Write(buf, read, nullptr);
            CloseHandle(hFile2);
            LARGE_INTEGER z = {};
            pStream->Seek(z, STREAM_SEEK_SET, nullptr);

            pDec = nullptr;
            hr = pF->CreateDecoderFromStream(pStream, &GUID_VendorMicrosoft,
                WICDecodeMetadataCacheOnDemand, &pDec);
            printf("CreateDecoderFromStream (with vendor): 0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                UINT w = 0, h = 0;
                IWICBitmapFrameDecode *pFrame = nullptr;
                pDec->GetFrame(0, &pFrame);
                if (pFrame)
                {
                    pFrame->GetSize(&w, &h);
                    printf("  Size: %u x %u\n", w, h);
                    pFrame->Release();
                }
                pDec->Release();
            }
            pStream->Release();
        }

        pF->Release();
    }

    CoUninitialize();
    return 0;
}
