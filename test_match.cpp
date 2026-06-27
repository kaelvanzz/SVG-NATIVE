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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *pF = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF));

    IWICBitmapDecoderInfo *pInfo = nullptr;
    pF->CreateComponentInfo(CLSID_SvgWicDecoder, (IWICComponentInfo**)&pInfo);

    if (pInfo)
    {
        // Check patterns count with buffer approach
        WICBitmapPattern pats[16] = {};
        UINT cnt = 0, actual = 0;
        HRESULT hr = pInfo->GetPatterns(sizeof(pats), pats, &cnt, &actual);
        printf("GetPatterns: 0x%08X (cnt=%u, actual=%u)\n", hr, cnt, actual);

        // Try MatchesPattern on various streams
        const char *testCases[] = {
            "<svg xmlns='http://www.w3.org/2000/svg'><circle/></svg>",
            "<?xml version=\"1.0\"?><svg xmlns='http://www.w3.org/2000/svg'/>",
            "GIF89a...",
        };
        for (int i = 0; i < 3; i++)
        {
            IStream *pS = nullptr;
            CreateStreamOnHGlobal(nullptr, TRUE, &pS);
            pS->Write(testCases[i], (ULONG)strlen(testCases[i]), nullptr);
            LARGE_INTEGER z = {};
            pS->Seek(z, STREAM_SEEK_SET, nullptr);
            BOOL match = FALSE;
            hr = pInfo->MatchesPattern(pS, &match);
            printf("  [%d] '%s' => hr=0x%08X match=%d\n", i, testCases[i], hr, (int)match);
            pS->Release();
        }

        // Now try CreateDecoderFromStream with our decoder directly
        {
            const char *svg = "<svg xmlns='http://www.w3.org/2000/svg'><circle/></svg>";
            IStream *pS = nullptr;
            CreateStreamOnHGlobal(nullptr, TRUE, &pS);
            pS->Write(svg, (ULONG)strlen(svg), nullptr);
            LARGE_INTEGER z = {};
            pS->Seek(z, STREAM_SEEK_SET, nullptr);
            IWICBitmapDecoder *pD = nullptr;
            hr = pF->CreateDecoderFromStream(pS, nullptr, WICDecodeMetadataCacheOnDemand, &pD);
            printf("CreateDecoderFromStream: 0x%08X\n", hr);
            if (SUCCEEDED(hr))
            {
                UINT w = 0, h = 0;
                IWICBitmapFrameDecode *pFr = nullptr;
                pD->GetFrame(0, &pFr);
                pFr->GetSize(&w, &h);
                printf("  => %u x %u\n", w, h);
                pFr->Release();
                pD->Release();
            }
            pS->Release();
        }

        pInfo->Release();
    }
    pF->Release();
    CoUninitialize();
    return 0;
}
