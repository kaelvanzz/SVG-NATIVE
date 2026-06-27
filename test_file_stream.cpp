#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

const CLSID CLSID_SvgWicDecoder = { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

int main()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Use a file-backed IStream (same as what WIC uses internally)
    IStream *pStream = nullptr;
    hr = SHCreateStreamOnFileEx(
        L"C:\\Users\\user\\Downloads\\discord-bot\\utils\\crypto\\bitcoin-btc-logo.svg",
        STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &pStream);
    printf("Open file stream: 0x%08X\n", hr);

    if (SUCCEEDED(hr))
    {
        // Check Stat
        STATSTG st = {};
        hr = pStream->Stat(&st, STATFLAG_NONAME);
        printf("Stat: 0x%08X, size=%lld\n", hr, st.cbSize.QuadPart);

        // Create our decoder
        IWICBitmapDecoder *pDec = nullptr;
        hr = CoCreateInstance(CLSID_SvgWicDecoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDec));
        printf("Create decoder: 0x%08X\n", hr);

        if (SUCCEEDED(hr))
        {
            // QueryCapability
            DWORD cap = 0;
            LARGE_INTEGER z = {};
            pStream->Seek(z, STREAM_SEEK_SET, nullptr);
            hr = pDec->QueryCapability(pStream, &cap);
            printf("QueryCapability: 0x%08X (cap=0x%X)\n", hr, cap);

            // Initialize
            pStream->Seek(z, STREAM_SEEK_SET, nullptr);
            hr = pDec->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
            printf("Initialize: 0x%08X\n", hr);

            if (SUCCEEDED(hr))
            {
                UINT count = 0;
                pDec->GetFrameCount(&count);
                printf("Frame count: %u\n", count);

                IWICBitmapFrameDecode *pFrame = nullptr;
                pDec->GetFrame(0, &pFrame);
                UINT w = 0, h = 0;
                pFrame->GetSize(&w, &h);
                printf("Size: %u x %u\n", w, h);
                pFrame->Release();
            }
            pDec->Release();
        }
        pStream->Release();
    }

    CoUninitialize();
    return 0;
}
