#include <windows.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// Include all the SVG rendering code directly
static void SanitizeSvg(char *buf);
static HRESULT GetSvgSizeFromBuffer(const char *buf, ULONG sz, float *pW, float *pH);
static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz);
static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels);

// Paste all the required functions
static HRESULT GetSvgSizeFromBuffer(const char *buf, ULONG sz, float *pW, float *pH)
{
    *pW = 0; *pH = 0;
    const char *svg = strstr(buf, "<svg");
    if (!svg || svg >= buf + sz) return S_FALSE;
    const char *endSvg = strchr(svg, '>');
    if (!endSvg || endSvg >= buf + sz) return S_FALSE;
    ptrdiff_t tagLen = endSvg - svg;
    if (tagLen <= 0 || tagLen > 4096) return S_FALSE;
    char *tag = (char*)malloc((size_t)tagLen + 1);
    if (!tag) return E_OUTOFMEMORY;
    memcpy(tag, svg, tagLen);
    tag[tagLen] = 0;
    char *w = strstr(tag, "width=\"");
    char *h = strstr(tag, "height=\"");
    float fw = 0, fh = 0;
    BOOL pctW = FALSE, pctH = FALSE;
    if (w) {
        sscanf_s(w + 7, "%f", &fw);
        if (strchr(w + 7, '%')) pctW = TRUE;
    }
    if (h) {
        sscanf_s(h + 8, "%f", &fh);
        if (strchr(h + 8, '%')) pctH = TRUE;
    }
    if (pctW || pctH) { fw = 0; fh = 0; }
    if (fw <= 0 || fh <= 0) {
        char *vb = strstr(tag, "viewBox=\"");
        if (vb) {
            vb += 9;
            for (char *p = vb; *p && *p != '"'; p++) if (*p == ',') *p = ' ';
            float vx, vy, vw, vh;
            if (sscanf_s(vb, "%f %f %f %f", &vx, &vy, &vw, &vh) == 4) {
                if (fw <= 0) fw = vw;
                if (fh <= 0) fh = vh;
            }
        }
    }
    free(tag);
    if (fw > 0 && fh > 0) { *pW = fw; *pH = fh; return S_OK; }
    return S_FALSE;
}

static void SanitizeSvg(char *buf)
{
    if (!buf) return;
    char *svg = strstr(buf, "<svg");
    if (!svg) return;
    char *end = strchr(svg, '>');
    if (!end) return;
    for (char *p = svg; p < end; p++)
    {
        if (_strnicmp(p, "enable-background", 17) == 0)
        {
            while (p < end && *p != ';' && *p != '"') *p++ = ' ';
            if (p < end && *p == ';') *p = ' ';
        }
    }
}

static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz)
{
    *out = nullptr;
    if (!strstr(in, "<style")) return S_FALSE;
    char *buf = (char*)malloc(inSz + 1);
    if (!buf) return E_OUTOFMEMORY;
    memcpy(buf, in, inSz);
    buf[inSz] = 0;
    struct { char cls[64]; char fill[64]; char stroke[64]; } rules[64];
    int nRules = 0;
    char *p = buf;
    while ((p = strstr(p, "<style")) && nRules < 64)
    {
        char *gt = strchr(p, '>');
        if (!gt) break;
        char *endStyle = strstr(gt, "</style>");
        if (!endStyle) break;
        char *r = gt + 1;
        while (r < endStyle && nRules < 64)
        {
            while (r < endStyle && *r != '.') r++;
            if (r >= endStyle) break;
            char *clsStart = r + 1;
            char *brace = strchr(clsStart, '{');
            if (!brace || brace > endStyle) break;
            int clen = (int)(brace - clsStart);
            while (clen > 0 && (clsStart[clen-1] == ' ' || clsStart[clen-1] == '\t')) clen--;
            if (clen <= 0 || clen >= 64) { r = brace + 1; continue; }
            char *close = strchr(brace, '}');
            if (!close || close > endStyle) break;
            char props[512] = {};
            int plen = (int)(close - brace - 1);
            if (plen > 0) { if (plen > 511) plen = 511; strncpy_s(props, brace + 1, plen); }
            char fillVal[64] = {}, strokeVal[64] = {};
            char *ctx = nullptr;
            char *tok = strtok_s(props, ";", &ctx);
            while (tok)
            {
                while (*tok == ' ' || *tok == '\t') tok++;
                char *col = strchr(tok, ':');
                if (col)
                {
                    *col++ = 0;
                    while (*col == ' ' || *col == '\t') col++;
                    char *pe = tok + strlen(tok) - 1;
                    while (pe > tok && (*pe == ' ' || *pe == '\t')) *pe-- = 0;
                    if (strcmp(tok, "fill") == 0) strcpy_s(fillVal, col);
                    else if (strcmp(tok, "stroke") == 0) strcpy_s(strokeVal, col);
                }
                tok = strtok_s(nullptr, ";", &ctx);
            }
            strncpy_s(rules[nRules].cls, clsStart, clen);
            rules[nRules].cls[clen] = 0;
            strcpy_s(rules[nRules].fill, fillVal);
            strcpy_s(rules[nRules].stroke, strokeVal);
            nRules++;
            r = close + 1;
        }
        memset(p, ' ', endStyle + 8 - p);
        p = endStyle + 8;
    }
    if (nRules == 0) { return S_FALSE; }
    char *outBuf = (char*)malloc(inSz * 2 + 1);
    if (!outBuf) { free(buf); return E_OUTOFMEMORY; }
    char *dst = outBuf;
    for (char *src = buf; *src; )
    {
        char *tag = strchr(src, '<');
        if (!tag) { size_t r = strlen(src); memcpy(dst, src, r); dst += r; break; }
        if (tag > src) { memcpy(dst, src, tag - src); dst += tag - src; }
        char *end = strchr(tag, '>');
        if (!end) { size_t r = strlen(tag); memcpy(dst, tag, r); dst += r; break; }
        char classVal[128] = {};
        char *cs = strstr(tag, "class=\"");
        if (cs && cs < end)
        {
            cs += 7;
            int ci = 0;
            while (cs + ci < end && ci < 127 && cs[ci] != '"') { classVal[ci] = cs[ci]; ci++; }
            classVal[ci] = 0;
        }
        BOOL selfClose = (end > tag && *(end-1) == '/');
        ptrdiff_t preLen = end - tag - (selfClose ? 1 : 0);
        memcpy(dst, tag, preLen);
        dst += preLen;
        if (classVal[0])
        {
            for (int ri = 0; ri < nRules; ri++)
            {
                char *fv = classVal;
                BOOL match = FALSE;
                while ((fv = strstr(fv, rules[ri].cls)))
                {
                    char pc = (fv > classVal) ? fv[-1] : ' ';
                    char nc = fv[strlen(rules[ri].cls)];
                    if (pc == ' ' && (nc == ' ' || nc == 0)) { match = TRUE; break; }
                    fv++;
                }
                if (!match) continue;
                if (rules[ri].fill[0] && !strstr(tag, " fill="))
                    dst += sprintf_s(dst, inSz*2-(dst-outBuf), " fill=\"%s\"", rules[ri].fill);
                if (rules[ri].stroke[0] && !strstr(tag, " stroke="))
                    dst += sprintf_s(dst, inSz*2-(dst-outBuf), " stroke=\"%s\"", rules[ri].stroke);
            }
        }
        if (selfClose) *dst++ = '/';
        *dst++ = '>';
        src = end + 1;
    }
    *dst = 0;
    *out = outBuf;
    *outSz = (ULONG)strlen(outBuf);
    return S_OK;
}

static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels)
{
    if (!pStream || !pixels || width == 0 || height == 0) return E_INVALIDARG;
    HRESULT hr;
    ID3D11Device *pD3D = nullptr;
    ID2D1Factory5 *pFactory = nullptr;
    ID2D1Device *pDevice = nullptr;
    ID2D1DeviceContext5 *pDC5 = nullptr;
    ID3D11Texture2D *pRT = nullptr;
    ID2D1Bitmap1 *pTarget = nullptr;
    ID2D1SvgDocument *pSvg = nullptr;
    IStream *pSvgStream = nullptr;
    ID3D11DeviceContext *pCtx = nullptr;
    ID3D11Texture2D *pStaging = nullptr;
    char *buf = nullptr, *svgBuf = nullptr, *inlined = nullptr;
    HGLOBAL hg = nullptr;
    STATSTG stat = {};
    LARGE_INTEGER z = {};

    if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) return E_FAIL;
    ULONG sz = (ULONG)stat.cbSize.QuadPart;
    if (sz == 0 || sz > 4194304) return E_FAIL;
    buf = (char*)malloc(sz + 1);
    if (!buf) return E_OUTOFMEMORY;
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG r = 0;
    pStream->Read(buf, sz, &r);
    buf[sz] = 0;
    svgBuf = buf;
    ULONG svgSz = sz;
    if (strstr(buf, "<style") && SUCCEEDED(InlineSvgCss(buf, sz, &inlined, &svgSz)) && inlined)
    { free(buf); svgBuf = inlined; }
    SanitizeSvg(svgBuf);
    hg = GlobalAlloc(GMEM_MOVEABLE, svgSz);
    if (!hg) { hr = E_OUTOFMEMORY; goto cleanup; }
    void *dst = GlobalLock(hg);
    if (dst) { CopyMemory(dst, svgBuf, svgSz); GlobalUnlock(hg); }
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &pSvgStream))) { hr = E_FAIL; goto cleanup; }
    hg = nullptr;

    float svgW = 0, svgH = 0;
    FLOAT vpW = (FLOAT)width * 2, vpH = (FLOAT)height * 2;
    {
        LARGE_INTEGER lz = {};
        pSvgStream->Seek(lz, STREAM_SEEK_SET, nullptr);
        STATSTG s2 = {};
        if (SUCCEEDED(pSvgStream->Stat(&s2, STATFLAG_NONAME)))
        {
            ULONG sz2 = (ULONG)s2.cbSize.QuadPart;
            if (sz2 > 0 && sz2 <= 65536)
            {
                char *buf2 = (char*)malloc(sz2 + 1);
                if (buf2)
                {
                    pSvgStream->Seek(z, STREAM_SEEK_SET, nullptr);
                    pSvgStream->Read(buf2, sz2, &r);
                    buf2[sz2] = 0;
                    GetSvgSizeFromBuffer(buf2, sz2, &svgW, &svgH);
                    free(buf2);
                    pSvgStream->Seek(lz, STREAM_SEEK_SET, nullptr);
                }
            }
        }
    }
    if (svgW > 0 && svgH > 0) { vpW = max(vpW, svgW); vpH = max(vpH, svgH); }
    UINT rtw = (UINT)min(2048.0f, max(vpW, (FLOAT)width));
    UINT rth = (UINT)min(2048.0f, max(vpH, (FLOAT)height));
    if (rtw < 1) rtw = 1; if (rth < 1) rth = 1;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &pD3D, nullptr, nullptr);
    if (FAILED(hr)) goto cleanup;
    IDXGIDevice *pDXGI = nullptr;
    hr = pD3D->QueryInterface(IID_PPV_ARGS(&pDXGI));
    if (FAILED(hr)) goto cleanup;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { pDXGI->Release(); goto cleanup; }
    hr = pFactory->CreateDevice(pDXGI, &pDevice);
    pDXGI->Release();
    if (FAILED(hr)) goto cleanup;
    ID2D1DeviceContext *pDC = nullptr;
    hr = pDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pDC);
    if (FAILED(hr)) { pDevice->Release(); goto cleanup; }
    hr = pDC->QueryInterface(IID_PPV_ARGS(&pDC5));
    pDC->Release();
    if (FAILED(hr)) { pDevice->Release(); goto cleanup; }
    pDevice->Release();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = rtw; td.Height = rth;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = pD3D->CreateTexture2D(&td, nullptr, &pRT);
    if (FAILED(hr)) goto cleanup;

    IDXGISurface *pSurf = nullptr;
    hr = pRT->QueryInterface(IID_PPV_ARGS(&pSurf));
    if (FAILED(hr)) goto cleanup;
    hr = pDC5->CreateBitmapFromDxgiSurface(pSurf, nullptr, &pTarget);
    pSurf->Release();
    if (FAILED(hr)) goto cleanup;

    LARGE_INTEGER lz = {};
    pSvgStream->Seek(lz, STREAM_SEEK_SET, nullptr);
    hr = pDC5->CreateSvgDocument(pSvgStream, D2D1::SizeF((FLOAT)rtw, (FLOAT)rth), &pSvg);
    if (FAILED(hr)) goto cleanup;

    __try {
        pDC5->SetTarget(pTarget);
        pDC5->BeginDraw();
        pDC5->Clear(D2D1::ColorF(0, 0, 0, 0));
        pDC5->DrawSvgDocument(pSvg);
        hr = pDC5->EndDraw();
    } __except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; }

    if (SUCCEEDED(hr))
    {
        pRT->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = pD3D->CreateTexture2D(&td, nullptr, &pStaging);
        if (FAILED(hr)) goto cleanup;
        pCtx = nullptr;
        pD3D->GetImmediateContext(&pCtx);
        if (pCtx)
        {
            pCtx->CopyResource(pStaging, pRT);
            D3D11_MAPPED_SUBRESOURCE map = {};
            if (SUCCEEDED(pCtx->Map(pStaging, 0, D3D11_MAP_READ, 0, &map)))
            {
                UINT srcStride = rtw * 4;
                UINT dstStride = width * 4;
                UINT cpyH = min(rth, height);
                for (UINT y = 0; y < cpyH; y++)
                    memcpy(pixels + y * dstStride, (BYTE*)map.pData + y * map.RowPitch, min(srcStride, dstStride));
                pCtx->Unmap(pStaging, 0);
            }
            pCtx->Release();
        }
    }

cleanup:
    if (pSvg) pSvg->Release();
    if (pTarget) pTarget->Release();
    if (pRT) pRT->Release();
    if (pDC5) pDC5->Release();
    if (pFactory) pFactory->Release();
    if (pD3D) pD3D->Release();
    if (pSvgStream) pSvgStream->Release();
    if (hg) GlobalFree(hg);
    if (svgBuf != buf && inlined) free(inlined);
    if (buf && !inlined) free(buf);
    if (pStaging) pStaging->Release();
    return hr;
}

static HRESULT SavePng(LPCWSTR path, UINT width, UINT height, UINT stride, BYTE *pixels)
{
    IWICImagingFactory *pWic = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWic));
    if (FAILED(hr)) return hr;
    IWICStream *pStream = nullptr;
    hr = pWic->CreateStream(&pStream);
    if (SUCCEEDED(hr))
    {
        hr = pStream->InitializeFromFilename(path, GENERIC_WRITE);
        IWICBitmapEncoder *pEnc = nullptr;
        if (SUCCEEDED(hr)) hr = pWic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEnc);
        if (SUCCEEDED(hr)) hr = pEnc->Initialize(pStream, WICBitmapEncoderNoCache);
        IWICBitmapFrameEncode *pFrame = nullptr;
        if (SUCCEEDED(hr)) hr = pEnc->CreateNewFrame(&pFrame, nullptr);
        if (SUCCEEDED(hr)) hr = pFrame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = pFrame->SetSize(width, height);
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = pFrame->SetPixelFormat(&pf);
        if (SUCCEEDED(hr)) hr = pFrame->WritePixels(height, stride, stride * height, pixels);
        if (SUCCEEDED(hr)) hr = pFrame->Commit();
        if (SUCCEEDED(hr)) hr = pEnc->Commit();
        if (pFrame) pFrame->Release();
        if (pEnc) pEnc->Release();
        pStream->Release();
    }
    pWic->Release();
    return hr;
}

static HRESULT CreateStreamFromFile(LPCWSTR path, IStream **ppStream)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) { CloseHandle(hFile); return E_FAIL; }
    ULONG sz = (ULONG)min(fileSize.QuadPart, 4194304LL);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hg) { CloseHandle(hFile); return E_OUTOFMEMORY; }
    BYTE *p = (BYTE*)GlobalLock(hg);
    DWORD read = 0;
    if (!ReadFile(hFile, p, sz, &read, nullptr) || read != sz)
    { GlobalUnlock(hg); GlobalFree(hg); CloseHandle(hFile); return E_FAIL; }
    GlobalUnlock(hg);
    CloseHandle(hFile);
    return CreateStreamOnHGlobal(hg, TRUE, ppStream);
}

int wmain(int argc, WCHAR *argv[])
{
    if (argc < 2) { wprintf(L"Usage: test_render.exe <svg-file>\n"); return 1; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IStream *pStream = nullptr;
    HRESULT hr = CreateStreamFromFile(argv[1], &pStream);
    if (FAILED(hr)) { wprintf(L"Cannot open file: 0x%08lx\n", hr); CoUninitialize(); return 1; }

    // Verify stream
    STATSTG stat = {};
    hr = pStream->Stat(&stat, STATFLAG_NONAME);
    wprintf(L"Stream Stat: 0x%08lx, size=%lld\n", hr, stat.cbSize.QuadPart);
    if (FAILED(hr)) { pStream->Release(); CoUninitialize(); return 1; }

    const UINT W = 256, H = 256;
    UINT stride = W * 4;
    BYTE *pixels = (BYTE*)malloc((size_t)stride * H);
    if (!pixels) { pStream->Release(); CoUninitialize(); return 1; }

    hr = RenderSvgToBuffer(pStream, W, H, stride, pixels);
    pStream->Release();

    wprintf(L"RenderSvgToBuffer: 0x%08lx\n", hr);

    // Save unique PNG
    WCHAR outPath[MAX_PATH];
    WCHAR *fn = wcsrchr(argv[1], '\\');
    if (!fn) fn = wcsrchr(argv[1], '/');
    if (!fn) fn = argv[1]; else fn++;
    swprintf_s(outPath, L"%s.svg.png", fn);

    SavePng(outPath, W, H, stride, pixels);
    wprintf(L"Saved: %s\n", outPath);

    free(pixels);
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
