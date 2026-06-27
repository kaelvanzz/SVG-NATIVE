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
    if (FAILED(hr)) { printf("CoInit: 0x%08X\n", hr); return 1; }

    // Create our decoder directly by CLSID
    IWICBitmapDecoder *pDecoder = nullptr;
    hr = CoCreateInstance(CLSID_SvgWicDecoder, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDecoder));
    if (FAILED(hr)) { printf("Create our decoder: 0x%08X\n", hr); CoUninitialize(); return 1; }
    printf("Our decoder created\n");

    // Open SVG file
    HANDLE hFile = CreateFileW(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { printf("File open failed\n"); pDecoder->Release(); CoUninitialize(); return 1; }

    // Read file into memory stream
    IStream *pStream = nullptr;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
    if (FAILED(hr)) { printf("CreateStream: 0x%08X\n", hr); CloseHandle(hFile); pDecoder->Release(); CoUninitialize(); return 1; }

    char buf[65536];
    DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
        pStream->Write(buf, read, nullptr);
    CloseHandle(hFile);

    // Rewind stream
    LARGE_INTEGER zero = {};
    pStream->Seek(zero, STREAM_SEEK_SET, nullptr);

    // Test QueryCapability
    DWORD cap = 0;
    hr = pDecoder->QueryCapability(pStream, &cap);
    printf("QueryCapability: 0x%08X (cap=0x%X)\n", hr, cap);

    // Rewind again
    pStream->Seek(zero, STREAM_SEEK_SET, nullptr);

    // Initialize
    hr = pDecoder->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
    printf("Initialize: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        UINT count = 0;
        pDecoder->GetFrameCount(&count);
        printf("Frame count: %u\n", count);

        IWICBitmapFrameDecode *pFrame = nullptr;
        hr = pDecoder->GetFrame(0, &pFrame);
        printf("GetFrame: 0x%08X\n", hr);

        if (SUCCEEDED(hr))
        {
            UINT w = 0, h = 0;
            pFrame->GetSize(&w, &h);
            printf("Size: %u x %u\n", w, h);

            WICRect rect = { 0, 0, (int)w, (int)h };
            UINT stride = w * 4;
            BYTE *pixels = (BYTE*)malloc(stride * h);
            hr = pFrame->CopyPixels(&rect, stride, stride * h, pixels);
            if (SUCCEEDED(hr))
            {
                BOOL hasContent = FALSE;
                for (UINT i = 0; i < stride * h; i++)
                    if (pixels[i] != 0) { hasContent = TRUE; break; }
                printf("CopyPixels OK, content=%s\n", hasContent ? "YES" : "NO");
            }
            else
                printf("CopyPixels: 0x%08X\n", hr);
            free(pixels);
            pFrame->Release();
        }
    }

    pStream->Release();
    pDecoder->Release();
    CoUninitialize();
    return 0;
}
