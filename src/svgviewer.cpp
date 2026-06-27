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

// ──── Helpers from thumbprovider.cpp ────

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
    // Debug: just find and remove enable-background only
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

static HRESULT GetSvgSizeFromStream(IStream *pStream, float *pW, float *pH)
{
    *pW = 0; *pH = 0;
    STATSTG stat = {};
    if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) return E_FAIL;
    ULONG sz = (ULONG)stat.cbSize.QuadPart;
    if (sz == 0 || sz > 65536) return E_FAIL;
    char *buf = (char*)malloc(sz + 1);
    if (!buf) return E_OUTOFMEMORY;
    LARGE_INTEGER z = {};
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG r = 0;
    pStream->Read(buf, sz, &r);
    buf[sz] = 0;
    HRESULT hr = GetSvgSizeFromBuffer(buf, sz, pW, pH);
    free(buf);
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    return hr;
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
                    char *pn = tok;
                    while (*col == ' ' || *col == '\t') col++;
                    char *pe = pn + strlen(pn) - 1;
                    while (pe > pn && (*pe == ' ' || *pe == '\t')) *pe-- = 0;
                    if (strcmp(pn, "fill") == 0) strcpy_s(fillVal, col);
                    else if (strcmp(pn, "stroke") == 0) strcpy_s(strokeVal, col);
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
        if (cs && cs < end) { cs += 7; int ci = 0; while (cs + ci < end && ci < 127 && cs[ci] != '"') { classVal[ci] = cs[ci]; ci++; } classVal[ci] = 0; }
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
    free(buf);
    *out = outBuf;
    *outSz = (ULONG)strlen(outBuf);
    return S_OK;
}

static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels)
{
    if (!pStream || !pixels || width == 0 || height == 0) return E_INVALIDARG;
    HRESULT hr;
    STATSTG stat = {};
    if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) return E_FAIL;
    ULONG sz = (ULONG)stat.cbSize.QuadPart;
    if (sz == 0 || sz > 4194304) return E_FAIL;
    char *buf = (char*)malloc(sz + 1);
    if (!buf) return E_OUTOFMEMORY;
    LARGE_INTEGER z = {};
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG r = 0;
    pStream->Read(buf, sz, &r);
    buf[sz] = 0;
    char *svgBuf = buf;
    ULONG svgSz = sz;
    char *inlined = nullptr;
    ULONG inlinedSz = 0;
    if (strstr(buf, "<style") && SUCCEEDED(InlineSvgCss(buf, sz, &inlined, &inlinedSz)) && inlined)
    { svgBuf = inlined; svgSz = inlinedSz; }
    // Save inlined SVG for debugging
    HANDLE hDbg = CreateFileW(L"C:\\Users\\user\\AppData\\Local\\Temp\\svg_inlined.svg", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hDbg != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(hDbg, svgBuf, svgSz, &w, nullptr); CloseHandle(hDbg); }
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, svgSz);
    IStream *pSvgStream = nullptr;
    if (hg)
    {
        void *dst = GlobalLock(hg);
        if (dst) { CopyMemory(dst, svgBuf, svgSz); GlobalUnlock(hg); }
        CreateStreamOnHGlobal(hg, TRUE, &pSvgStream);
    }
    if (svgBuf != buf) free(svgBuf);
    else free(buf);
    if (!pSvgStream) return E_OUTOFMEMORY;
    float svgW = 0, svgH = 0;
    FLOAT vpW = (FLOAT)width * 2, vpH = (FLOAT)height * 2;
    GetSvgSizeFromStream(pSvgStream, &svgW, &svgH);
    if (svgW > 0 && svgH > 0) { vpW = max(vpW, svgW); vpH = max(vpH, svgH); }
    UINT rtw = (UINT)min(2048.0f, max(vpW, (FLOAT)width));
    UINT rth = (UINT)min(2048.0f, max(vpH, (FLOAT)height));
    if (rtw < 1) rtw = 1; if (rth < 1) rth = 1;
    ID3D11Device *pD3D = nullptr;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &pD3D, nullptr, nullptr);
    if (FAILED(hr)) { pSvgStream->Release(); return E_FAIL; }
    IDXGIDevice *pDXGI = nullptr;
    hr = pD3D->QueryInterface(IID_PPV_ARGS(&pDXGI));
    if (FAILED(hr)) { pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID2D1Factory5 *pFactory = nullptr;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { pDXGI->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID2D1Device *pDevice = nullptr;
    hr = pFactory->CreateDevice(pDXGI, &pDevice);
    if (FAILED(hr)) { pFactory->Release(); pDXGI->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID2D1DeviceContext *pDC = nullptr;
    hr = pDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pDC);
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID2D1DeviceContext5 *pDC5 = nullptr;
    hr = pDC->QueryInterface(IID_PPV_ARGS(&pDC5));
    pDC->Release();
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    pDevice->Release(); pDXGI->Release();
    ID3D11Texture2D *pRT = nullptr;
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = rtw; td.Height = rth; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = pD3D->CreateTexture2D(&td, nullptr, &pRT);
    }
    if (FAILED(hr)) { pDC5->Release(); pFactory->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    IDXGISurface *pSurf = nullptr;
    hr = pRT->QueryInterface(IID_PPV_ARGS(&pSurf));
    if (FAILED(hr)) { pRT->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID2D1Bitmap1 *pTarget = nullptr;
    hr = pDC5->CreateBitmapFromDxgiSurface(pSurf, nullptr, &pTarget);
    pSurf->Release();
    if (FAILED(hr)) { pRT->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    LARGE_INTEGER lz = {};
    pSvgStream->Seek(lz, STREAM_SEEK_SET, nullptr);
    ID2D1SvgDocument *pSvg = nullptr;
    hr = pDC5->CreateSvgDocument(pSvgStream, D2D1::SizeF((FLOAT)rtw, (FLOAT)rth), &pSvg);
    if (FAILED(hr)) { pTarget->Release(); pRT->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); pSvgStream->Release(); return E_FAIL; }
    ID3D11DeviceContext *pCtx = nullptr;
    hr = S_OK;
    __try
    {
        pDC5->SetTarget(pTarget);
        pDC5->BeginDraw();
        pDC5->Clear(D2D1::ColorF(0, 0, 0, 0));
        pDC5->DrawSvgDocument(pSvg);
        hr = pDC5->EndDraw();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC td = {};
        pRT->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        ID3D11Texture2D *pStaging = nullptr;
        if (SUCCEEDED(pD3D->CreateTexture2D(&td, nullptr, &pStaging)))
        {
            pD3D->GetImmediateContext(&pCtx);
            pCtx->CopyResource(pStaging, pRT);
            D3D11_MAPPED_SUBRESOURCE map = {};
            if (SUCCEEDED(pCtx->Map(pStaging, 0, D3D11_MAP_READ, 0, &map)))
            {
                BYTE *src = (BYTE*)map.pData;
                int srcW = (int)(width * (rtw / (FLOAT)max(rtw, rth)));
                int srcH = (int)(height * (rth / (FLOAT)max(rtw, rth)));
                int xOff = ((int)width - srcW) / 2;
                int yOff = ((int)height - srcH) / 2;
                ZeroMemory(pixels, (size_t)stride * height);
                FLOAT stepX = (FLOAT)rtw / (FLOAT)srcW;
                FLOAT stepY = (FLOAT)rth / (FLOAT)srcH;
                for (int y = 0; y < srcH && yOff + y < (int)height; y++)
                {
                    FLOAT sy = y * stepY;
                    int iy0 = (int)sy, iy1 = min(iy0 + 1, (int)rth - 1);
                    FLOAT fy = sy - iy0;
                    BYTE *dstLine = pixels + (yOff + y) * stride + xOff * 4;
                    for (int x = 0; x < srcW && xOff + x < (int)width; x++)
                    {
                        FLOAT sx = x * stepX;
                        int ix0 = (int)sx, ix1 = min(ix0 + 1, (int)rtw - 1);
                        FLOAT fx = sx - ix0;
                        for (int c = 0; c < 4; c++)
                        {
                            FLOAT v = (1-fy)*((1-fx)*src[iy0*map.RowPitch+ix0*4+c]+fx*src[iy0*map.RowPitch+ix1*4+c])
                                    + fy *((1-fx)*src[iy1*map.RowPitch+ix0*4+c]+fx*src[iy1*map.RowPitch+ix1*4+c]);
                            dstLine[x*4+c] = (BYTE)(v + 0.5f);
                        }
                    }
                }
                pCtx->Unmap(pStaging, 0);
            }
            pStaging->Release();
        }
    }
    if (pCtx) pCtx->Release();
    pSvg->Release(); pTarget->Release(); pRT->Release();
    pDC5->Release(); pFactory->Release(); pD3D->Release();
    pSvgStream->Release();
    return hr;
}

static HRESULT SavePng(const WCHAR *path, UINT w, UINT h, UINT stride, BYTE *pixels)
{
    IWICImagingFactory *pWic = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWic));
    if (FAILED(hr)) return hr;

    IWICStream *pStream = nullptr;
    hr = pWic->CreateStream(&pStream);
    if (SUCCEEDED(hr))
    {
        hr = pStream->InitializeFromFilename(path, GENERIC_WRITE);
        if (SUCCEEDED(hr))
        {
            IWICBitmapEncoder *pEncoder = nullptr;
            hr = pWic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
            if (SUCCEEDED(hr))
            {
                hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
                if (SUCCEEDED(hr))
                {
                    IWICBitmapFrameEncode *pFrame = nullptr;
                    IPropertyBag2 *pProps = nullptr;
                    hr = pEncoder->CreateNewFrame(&pFrame, &pProps);
                    if (SUCCEEDED(hr))
                    {
                        hr = pFrame->Initialize(pProps);
                        if (SUCCEEDED(hr))
                        {
                            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                            pFrame->SetSize(w, h);
                            pFrame->SetPixelFormat(&fmt);
                            hr = pFrame->WritePixels(h, stride, stride * h, pixels);
                            if (SUCCEEDED(hr))
                                pFrame->Commit();
                        }
                        pFrame->Release();
                    }
                    if (pProps) pProps->Release();
                    pEncoder->Commit();
                }
                pEncoder->Release();
            }
        }
        pStream->Release();
    }
    pWic->Release();
    return hr;
}

int wmain(int argc, WCHAR *argv[])
{
    if (argc < 2)
    {
        MessageBoxW(nullptr, L"Usage: SvgViewer.exe <svg-file>", L"SVG Viewer", MB_OK);
        return 1;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Open SVG file as stream
    IStream *pStream = nullptr;
    HANDLE hFile = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(nullptr, L"Cannot open SVG file", L"Error", MB_OK);
        CoUninitialize();
        return 1;
    }

    CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
    char buf[262144]; DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
        pStream->Write(buf, read, nullptr);
    CloseHandle(hFile);
    LARGE_INTEGER z = {};
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);

    // Render to 256x256 BGRA buffer (Photos opens it at natural size anyway)
    const UINT W = 1024, H = 1024;
    UINT stride = W * 4;
    BYTE *pixels = (BYTE*)malloc((size_t)stride * H);
    if (!pixels) { pStream->Release(); CoUninitialize(); return 1; }

    HRESULT hr = RenderSvgToBuffer(pStream, W, H, stride, pixels);
    pStream->Release();

    if (FAILED(hr))
    {
        WCHAR msg[128]; swprintf_s(msg, L"Failed to render SVG: 0x%08lx", hr);
        MessageBoxW(nullptr, msg, L"Error", MB_OK);
        free(pixels);
        CoUninitialize();
        return 1;
    }

    // Save to temp PNG
    WCHAR tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    WCHAR pngPath[MAX_PATH];
    swprintf_s(pngPath, L"%s\\svgview_%u.png", tempPath, GetTickCount());

    hr = SavePng(pngPath, W, H, stride, pixels);
    free(pixels);

    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"Failed to save PNG", L"Error", MB_OK);
        CoUninitialize();
        return 1;
    }

    // Open with default photo viewer (Photos)
    ShellExecuteW(nullptr, L"open", pngPath, nullptr, nullptr, SW_SHOW);

    CoUninitialize();
    return 0;
}
