#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <strsafe.h>
#include <new>
#include "minhook/MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

// ---- forward declarations ---------------------------------------
static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz);
static HRESULT GetSvgSizeFromBuffer(const char *buf, ULONG sz, float *pW, float *pH);
static HRESULT GetSvgSizeFromStream(IStream *pStream, float *pW, float *pH);
static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels);

// GUIDs (declare locally to avoid linker dependency on uuid.lib for these)
static const GUID GUID_ContainerFormatSvg =
    { 0xa6ba1b82, 0x2489, 0x4b33, { 0x9f, 0x3a, 0xca, 0x6b, 0x5c, 0x3a, 0x9a, 0x4b } };

// ---- WIC frame decode -------------------------------------------
class CSvgWicFrameDecode : public IWICBitmapFrameDecode
{
public:
    CSvgWicFrameDecode(IStream *pStream, UINT w, UINT h) : m_cRef(1), m_pStream(pStream), m_w(w), m_h(h)
    {
        if (m_pStream) m_pStream->AddRef();
    }
    ~CSvgWicFrameDecode() { if (m_pStream) m_pStream->Release(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IWICBitmapSource || riid == IID_IWICBitmapFrameDecode)
        {
            *ppv = static_cast<IWICBitmapFrameDecode*>(this);
            AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP GetSize(UINT *pw, UINT *ph) { if (pw) *pw = m_w; if (ph) *ph = m_h; return S_OK; }
    IFACEMETHODIMP GetPixelFormat(WICPixelFormatGUID *pf) { if (pf) *pf = GUID_WICPixelFormat32bppBGRA; return S_OK; }
    IFACEMETHODIMP GetResolution(double *dx, double *dy) { if (dx) *dx = 96; if (dy) *dy = 96; return S_OK; }

    IFACEMETHODIMP CopyPixels(WICRect const *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer)
    {
        if (!pbBuffer) return E_POINTER;
        UINT stride = m_w * 4;
        BYTE *tmp = (BYTE*)malloc((size_t)stride * m_h);
        if (!tmp) return E_OUTOFMEMORY;
        LARGE_INTEGER z = {};
        m_pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        HRESULT hr = RenderSvgToBuffer(m_pStream, m_w, m_h, stride, tmp);
        if (FAILED(hr)) { free(tmp); return hr; }
        if (!prc)
        {
            if (cbBufferSize < stride * m_h) { free(tmp); return WINCODEC_ERR_INSUFFICIENTBUFFER; }
            CopyMemory(pbBuffer, tmp, (size_t)stride * m_h);
        }
        else
        {
            for (int y = 0; y < prc->Height && (y + prc->Y) < (int)m_h; y++)
            {
                int sy = prc->Y + y;
                if (sy < 0) continue;
                int sx = prc->X, cw = prc->Width;
                if (sx < 0) { cw += sx; sx = 0; }
                if (sx >= (int)m_w || cw <= 0) continue;
                if (sx + cw > (int)m_w) cw = m_w - sx;
                CopyMemory(pbBuffer + y * cbStride, tmp + sy * stride + sx * 4, (size_t)cw * 4);
            }
        }
        free(tmp);
        return S_OK;
    }

    IFACEMETHODIMP CopyPalette(IWICPalette *) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetThumbnail(IWICBitmapSource **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetColorContexts(UINT, IWICColorContext **, UINT *) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }

private:
    LONG m_cRef;
    IStream *m_pStream;
    UINT m_w, m_h;
};

// ---- WIC decoder -----------------------------------------------
class CSvgWicDecoder : public IWICBitmapDecoder
{
public:
    CSvgWicDecoder() : m_cRef(1), m_pStream(nullptr), m_w(256), m_h(256), m_init(FALSE) {}
    ~CSvgWicDecoder() { if (m_pStream) m_pStream->Release(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IWICBitmapDecoder)
        {
            *ppv = static_cast<IWICBitmapDecoder*>(this);
            AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP QueryCapability(IStream *pStream, DWORD *pCap)
    {
        if (!pStream || !pCap) return E_POINTER;
        *pCap = 0;
        char buf[256] = {};
        LARGE_INTEGER z = {};
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        ULONG r = 0;
        pStream->Read(buf, 255, &r);
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        if (strstr(buf, "<svg") || strstr(buf, "<SVG"))
            *pCap = WICBitmapDecoderCapabilityCanDecodeAllImages |
                    WICBitmapDecoderCapabilityCanDecodeSomeImages |
                    WICBitmapDecoderCapabilityCanEnumerateMetadata;
        return S_OK;
    }

    IFACEMETHODIMP Initialize(IStream *pStream, WICDecodeOptions)
    {
        if (m_init) return WINCODEC_ERR_WRONGSTATE;
        LARGE_INTEGER z = {};
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        STATSTG stat = {};
        if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) return E_FAIL;
        ULONG sz = (ULONG)stat.cbSize.QuadPart;
        if (sz == 0 || sz > 4194304) return WINCODEC_ERR_BADSTREAMDATA;
        char *buf = (char*)malloc(sz + 1);
        if (!buf) return E_OUTOFMEMORY;
        ULONG r = 0;
        pStream->Read(buf, sz, &r);
        buf[sz] = 0;
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        if (!strstr(buf, "<svg") && !strstr(buf, "<SVG"))
        { free(buf); return WINCODEC_ERR_BADSTREAMDATA; }
        float fw = 0, fh = 0;
        GetSvgSizeFromBuffer(buf, sz, &fw, &fh);
        free(buf);
        if (fw > 0 && fh > 0) { m_w = (UINT)fw; m_h = (UINT)fh; }
        if (m_w < 1) m_w = 1; if (m_h < 1) m_h = 1;
        m_pStream = pStream;
        m_pStream->AddRef();
        m_init = TRUE;
        return S_OK;
    }

    IFACEMETHODIMP GetContainerFormat(GUID *pG) { if (pG) *pG = GUID_ContainerFormatSvg; return S_OK; }
    IFACEMETHODIMP GetFrameCount(UINT *pC) { if (pC) *pC = 1; return S_OK; }

    IFACEMETHODIMP GetFrame(UINT idx, IWICBitmapFrameDecode **ppF)
    {
        if (!ppF) return E_POINTER;
        *ppF = nullptr;
        if (idx != 0) return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
        if (!m_init) return WINCODEC_ERR_WRONGSTATE;
        CSvgWicFrameDecode *pF = new (std::nothrow) CSvgWicFrameDecode(m_pStream, m_w, m_h);
        if (!pF) return E_OUTOFMEMORY;
        *ppF = pF;
        pF->AddRef();
        return S_OK;
    }

    IFACEMETHODIMP GetDecoderInfo(IWICBitmapDecoderInfo **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP CopyPalette(IWICPalette *) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetMetadataQueryReader(IWICMetadataQueryReader **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetPreview(IWICBitmapSource **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetColorContexts(UINT, IWICColorContext **, UINT *) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    IFACEMETHODIMP GetThumbnail(IWICBitmapSource **) { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }

private:
    LONG m_cRef;
    IStream *m_pStream;
    UINT m_w, m_h;
    BOOL m_init;
};

// ---- CSS inliner ------------------------------------------------
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
    *out = outBuf;
    *outSz = (ULONG)strlen(outBuf);
    return S_OK;
}

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
            continue;
        }
        if (_strnicmp(p, "xml:space", 9) == 0)
        {
            while (p < end && *p != ' ') *p++ = ' ';
            continue;
        }
        if ((*p == 'x' || *p == 'y') && *(p+1) == '=')
        {
            char *val = p + 2;
            if (*val == '"' && (*(val+1) == '0' || _strnicmp(val+1, "0px", 3) == 0))
            {
                BOOL ok = TRUE;
                char *q = val + 1; while (*q && *q != '"') q++;
                if (*q != '"') ok = FALSE;
                if (ok) { while (p <= q) *p++ = ' '; continue; }
            }
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

// ---- D2D renderer ----------------------------------------------
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
    if (strstr(buf, "<style") && SUCCEEDED(InlineSvgCss(buf, sz, &inlined, &svgSz)) && inlined)
    { free(buf); svgBuf = inlined; }
    SanitizeSvg(svgBuf);
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
    __try {
        pDC5->SetTarget(pTarget);
        pDC5->BeginDraw();
        pDC5->Clear(D2D1::ColorF(0, 0, 0, 0));
        pDC5->DrawSvgDocument(pSvg);
        hr = pDC5->EndDraw();
    } __except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; }

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

// ---- MinHook: hook IWICImagingFactory vtable --------------------

// Original function pointers (trampolines)
static HRESULT (STDMETHODCALLTYPE *Real_CreateDecoderFromFilename)(
    IWICImagingFactory *, LPCWSTR, const GUID *, DWORD, WICDecodeOptions, IWICBitmapDecoder **) = nullptr;
static HRESULT (STDMETHODCALLTYPE *Real_CreateDecoderFromStream)(
    IWICImagingFactory *, IStream *, const GUID *, WICDecodeOptions, IWICBitmapDecoder **) = nullptr;

static BOOL g_hooksReady = FALSE;
static CRITICAL_SECTION g_hookCs;

static BOOL IsSvgExtension(LPCWSTR path)
{
    if (!path) return FALSE;
    size_t len = wcslen(path);
    if (len < 5) return FALSE;
    // Check for .svg or .SVG
    LPCWSTR dot = path + len - 4;
    return (dot[0] == L'.' || dot[0] == L'.') &&
           (dot[1] == L's' || dot[1] == L'S') &&
           (dot[2] == L'v' || dot[2] == L'V') &&
           (dot[3] == L'g' || dot[3] == L'G');
}

static BOOL IsSvgStream(IStream *pStream)
{
    if (!pStream) return FALSE;
    char buf[256] = {};
    LARGE_INTEGER z = {};
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG r = 0;
    pStream->Read(buf, 255, &r);
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    return (strstr(buf, "<svg") || strstr(buf, "<SVG"));
}

static HRESULT CreateSvgDecoderFromStream(IStream *pStream, IWICBitmapDecoder **ppDecoder)
{
    CSvgWicDecoder *pDec = new (std::nothrow) CSvgWicDecoder();
    if (!pDec) return E_OUTOFMEMORY;
    HRESULT hr = pDec->Initialize(pStream, WICDecodeMetadataCacheOnDemand);
    if (FAILED(hr)) { pDec->Release(); return hr; }
    hr = pDec->QueryInterface(IID_IWICBitmapDecoder, (void**)ppDecoder);
    pDec->Release();
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDecoderFromFilename(
    IWICImagingFactory *pThis,
    LPCWSTR wzFilename,
    const GUID *pguidVendor,
    DWORD dwDesiredAccess,
    WICDecodeOptions metadataOptions,
    IWICBitmapDecoder **ppIDecoder)
{
    if (!g_hooksReady)
        return Real_CreateDecoderFromFilename(pThis, wzFilename, pguidVendor, dwDesiredAccess, metadataOptions, ppIDecoder);

    if (IsSvgExtension(wzFilename))
    {
        HANDLE hFile = CreateFileW(wzFilename, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        IStream *pStream = nullptr;
        HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
        if (FAILED(hr)) { CloseHandle(hFile); return hr; }

        char buf[8192];
        DWORD read = 0;
        while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
        {
            ULONG written = 0;
            pStream->Write(buf, read, &written);
        }
        CloseHandle(hFile);

        LARGE_INTEGER z = {};
        pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        hr = CreateSvgDecoderFromStream(pStream, ppIDecoder);
        pStream->Release();
        return hr;
    }

    return Real_CreateDecoderFromFilename(pThis, wzFilename, pguidVendor, dwDesiredAccess, metadataOptions, ppIDecoder);
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDecoderFromStream(
    IWICImagingFactory *pThis,
    IStream *pIStream,
    const GUID *pguidVendor,
    WICDecodeOptions metadataOptions,
    IWICBitmapDecoder **ppIDecoder)
{
    if (!g_hooksReady)
        return Real_CreateDecoderFromStream(pThis, pIStream, pguidVendor, metadataOptions, ppIDecoder);

    if (IsSvgStream(pIStream))
    {
        LARGE_INTEGER z = {};
        pIStream->Seek(z, STREAM_SEEK_SET, nullptr);
        return CreateSvgDecoderFromStream(pIStream, ppIDecoder);
    }

    return Real_CreateDecoderFromStream(pThis, pIStream, pguidVendor, metadataOptions, ppIDecoder);
}

// ---- Init thread ------------------------------------------------
static DWORD WINAPI InitHookThread(LPVOID)
{
    Sleep(200); // Wait for process to settle

    // Wait for windowscodecs.dll to be loaded (required to read IWICImagingFactory vtable)
    HMODULE hWic = GetModuleHandleW(L"windowscodecs.dll");
    int tries = 0;
    while (!hWic && tries < 100)
    {
        Sleep(50);
        hWic = GetModuleHandleW(L"windowscodecs.dll");
        tries++;
    }
    if (!hWic) return 0;

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) return 0;

    IWICImagingFactory *pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (SUCCEEDED(hr))
    {
        void **vtable = *(void***)pFactory;

        if (MH_Initialize() == MH_OK)
        {
            MH_STATUS mh;

            mh = MH_CreateHook(vtable[3], Hook_CreateDecoderFromFilename,
                (void**)&Real_CreateDecoderFromFilename);
            if (mh == MH_OK)
            {
                mh = MH_CreateHook(vtable[4], Hook_CreateDecoderFromStream,
                    (void**)&Real_CreateDecoderFromStream);
            }

            if (mh == MH_OK)
            {
                MH_EnableHook(MH_ALL_HOOKS);
                g_hooksReady = TRUE;
            }

            // Signal that hooks are ready (keep handle open so named event persists)
            HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\SVG_WIC_Hook_Ready");
            if (hEvent) { SetEvent(hEvent); } // Don't CloseHandle - leave for other processes to find
        }

        pFactory->Release();
    }

    if (SUCCEEDED(coHr)) CoUninitialize();
    return 0;
}

// ---- Exported function for SetWindowsHookEx ---------------------
extern "C" __declspec(dllexport) LRESULT CALLBACK CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // One-time: set up hooks and remove this hook
    static volatile LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) == 0)
    {
        HANDLE hThread = CreateThread(nullptr, 0, InitHookThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

extern "C" __declspec(dllexport) void InstallHooks(void)
{
    // Called directly (e.g. from AppInit_DLLs via DllMain thread)
    // or from CbtHookProc.  Start the init thread.
    HANDLE hThread = CreateThread(nullptr, 0, InitHookThread, nullptr, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// ---- DllMain ----------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        if (g_hooksReady)
        {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
        break;
    }
    return TRUE;
}
