#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <strsafe.h>
#include <new>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

// CLSID_SvgWicDecoder = {11E7785D-7BFE-411C-AD88-48849C9EE8B1}
static const CLSID CLSID_SvgWicDecoder =
    { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };
static const GUID GUID_ContainerFormatSvg =
    { 0xa6ba1b82, 0x2489, 0x4b33, { 0x9f, 0x3a, 0xca, 0x6b, 0x5c, 0x3a, 0x9a, 0x4b } };

static HINSTANCE g_hInst;
static long g_cLock;
static CRITICAL_SECTION g_cs;
static BOOL g_csInit = FALSE;

#define VENDOR_GUID_SVG L"{F0E749CA-EDEF-4589-A73A-EE0E626A2A2B}"
static const GUID GUID_VendorSvg =
    { 0xf0e749ca, 0xedef, 0x4589, { 0xa7, 0x3a, 0xee, 0x0e, 0x62, 0x6a, 0x2a, 0x2b } };

// ---- forward declarations ---------------------------------------
static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz);
static HRESULT GetSvgSizeFromBuffer(const char *buf, ULONG sz, float *pW, float *pH);
static HRESULT GetSvgSizeFromStream(IStream *pStream, float *pW, float *pH);
static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels);

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

// ---- WIC decoder -------------------------------------------------
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

// ---- class factory -----------------------------------------------
class CSvgWicDecoderFactory : public IClassFactory
{
public:
    CSvgWicDecoderFactory() : m_cRef(1) {}
    virtual ~CSvgWicDecoderFactory() {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppv = static_cast<IClassFactory*>(this);
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
    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv)
    {
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CSvgWicDecoder *pDec = new (std::nothrow) CSvgWicDecoder();
        if (!pDec) return E_OUTOFMEMORY;
        HRESULT hr = pDec->QueryInterface(riid, ppv);
        pDec->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) { return S_OK; }

private:
    LONG m_cRef;
};

// ---- DLL exports --------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    g_hInst = hModule;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&g_cs);
        g_csInit = TRUE;
        DisableThreadLibraryCalls(hModule);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        if (g_csInit) { DeleteCriticalSection(&g_cs); g_csInit = FALSE; }
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_SvgWicDecoder))
        return CLASS_E_CLASSNOTAVAILABLE;
    CSvgWicDecoderFactory *pFactory = new (std::nothrow) CSvgWicDecoderFactory();
    if (!pFactory) return E_OUTOFMEMORY;
    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return g_cLock > 0 ? S_FALSE : S_OK;
}

static void RegWicDecoder(const WCHAR *modulePath)
{
    HKEY hKey;
    WCHAR keyPath[MAX_PATH];
    LPCWSTR clsid = L"{11E7785D-7BFE-411C-AD88-48849C9EE8B1}";
    LPCWSTR fmtGUID = L"{a6ba1b82-2489-4b33-9f3a-ca6b5c3a9a4b}";
    LPCWSTR cat = L"{7ED96837-96F0-4812-B211-F13C24117ED3}";
    LPCWSTR vendor = L"{F0E749CA-EDEF-4589-A73A-EE0E626A2A2B}";
    DWORD flags = 0x3;
    DWORD priority = 0x1;

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s", clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SVG WIC Decoder", 30);
        RegSetValueExW(hKey, L"ContainerFormat", 0, REG_SZ, (BYTE*)fmtGUID, (DWORD)((wcslen(fmtGUID) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ, (BYTE*)L"SVG WIC Decoder", 30);
        RegSetValueExW(hKey, L"Description", 0, REG_SZ, (BYTE*)L"SVG WIC Decoder", 30);
        RegSetValueExW(hKey, L"Author", 0, REG_SZ, (BYTE*)L"NATIVE", 14);
        RegSetValueExW(hKey, L"Vendor", 0, REG_SZ, (BYTE*)vendor, (DWORD)((wcslen(vendor) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"FileExtensions", 0, REG_SZ, (BYTE*)L".svg", 8);
        RegSetValueExW(hKey, L"MimeTypes", 0, REG_SZ, (BYTE*)L"image/svg+xml", 26);
        RegSetValueExW(hKey, L"SpecVersion", 0, REG_SZ, (BYTE*)L"1.0.0.0", 14);
        RegSetValueExW(hKey, L"Version", 0, REG_SZ, (BYTE*)L"1.0.0.0", 14);
        RegSetValueExW(hKey, L"Flags", 0, REG_DWORD, (BYTE*)&flags, sizeof(flags));
        RegSetValueExW(hKey, L"ArbitrationPriority", 0, REG_DWORD, (BYTE*)&priority, sizeof(priority));
        RegCloseKey(hKey);
    }

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\InProcServer32", clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        DWORD len = (DWORD)((wcslen(modulePath) + 1) * sizeof(WCHAR));
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)modulePath, len);
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Both", 10);
        RegCloseKey(hKey);
    }

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Categories\\%s", clsid, cat);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        RegCloseKey(hKey);

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Instance\\%s", cat, clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ, (BYTE*)L"SVG WIC Decoder", 30);
        RegCloseKey(hKey);
    }

    LPCWSTR fmt32 = L"{6FDDC324-4E03-4BFE-B185-3D77768DC90D}";
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Formats\\%s", clsid, fmt32);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        RegCloseKey(hKey);

    StringCchPrintfW(keyPath, MAX_PATH, L"MIME\\Database\\Content Type\\image/svg+xml");
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"Extension", 0, REG_SZ, (BYTE*)L".svg", 10);
        RegCloseKey(hKey);
    }

    // Patterns at multiple offsets for WIC stream-based discovery
    BYTE svgPattern[4] = { 0x3C, 0x73, 0x76, 0x67 }; // "<svg"
    BYTE svgMask[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    for (int i = 0; i <= 15; i++)
    {
        DWORD offset = (DWORD)(i * 20);
        StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Patterns\\%d", clsid, i);
        if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        {
            RegSetValueExW(hKey, L"Pattern", 0, REG_BINARY, svgPattern, 4);
            RegSetValueExW(hKey, L"Mask", 0, REG_BINARY, svgMask, 4);
            RegSetValueExW(hKey, L"StartOffset", 0, REG_DWORD, (BYTE*)&offset, sizeof(offset));
            RegCloseKey(hKey);
        }
    }

    // Also match <?xml at offset 0 (covers XML-declared SVGs)
    BYTE xmlPattern[5] = { 0x3C, 0x3F, 0x78, 0x6D, 0x6C }; // "<?xml"
    BYTE xmlMask[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Patterns\\16", clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, L"Pattern", 0, REG_BINARY, xmlPattern, 5);
        RegSetValueExW(hKey, L"Mask", 0, REG_BINARY, xmlMask, 5);
        DWORD zero = 0;
        RegSetValueExW(hKey, L"StartOffset", 0, REG_DWORD, (BYTE*)&zero, sizeof(zero));
        RegCloseKey(hKey);
    }
}

extern "C" HRESULT __stdcall DllRegisterServer()
{
    WCHAR szModulePath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, szModulePath, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    RegWicDecoder(szModulePath);
    return S_OK;
}

extern "C" HRESULT __stdcall DllUnregisterServer()
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"MIME\\Database\\Content Type\\image/svg+xml");
    return S_OK;
}

// ---- CSS inliner --------------------------------------------------
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

// ---- D2D renderer -------------------------------------------------
static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels)
{
    HRESULT hr = E_FAIL;
    IStream *pSvgStream = nullptr;
    ID3D11Device *pD3D = nullptr;
    ID2D1Factory5 *pFactory = nullptr;
    ID2D1DeviceContext5 *pDC5 = nullptr;
    ID3D11Texture2D *pRT = nullptr;
    ID2D1Bitmap1 *pTarget = nullptr;
    ID2D1SvgDocument *pSvg = nullptr;
    ID3D11DeviceContext *pCtx = nullptr;
    ID3D11Texture2D *pStaging = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    char *buf = nullptr;
    char *svgBuf = nullptr;
    char *inlined = nullptr;
    HGLOBAL hg = nullptr;
    STATSTG stat = {};
    LARGE_INTEGER z = {};
    LARGE_INTEGER lz = {};

    EnterCriticalSection(&g_cs);
    if (!pStream || !pixels || width == 0 || height == 0) { LeaveCriticalSection(&g_cs); return E_INVALIDARG; }
    if (FAILED(pStream->Stat(&stat, STATFLAG_NONAME))) { LeaveCriticalSection(&g_cs); return E_FAIL; }
    ULONG sz = (ULONG)stat.cbSize.QuadPart;
    if (sz == 0 || sz > 4194304) { LeaveCriticalSection(&g_cs); return E_FAIL; }
    buf = (char*)malloc(sz + 1);
    if (!buf) { LeaveCriticalSection(&g_cs); return E_OUTOFMEMORY; }
    pStream->Seek(z, STREAM_SEEK_SET, nullptr);
    ULONG r = 0;
    pStream->Read(buf, sz, &r);
    buf[sz] = 0;
    svgBuf = buf;
    ULONG svgSz = sz;
    if (strstr(buf, "<style") && SUCCEEDED(InlineSvgCss(buf, sz, &inlined, &svgSz)) && inlined)
    { svgBuf = inlined; }
    SanitizeSvg(svgBuf);
    hg = GlobalAlloc(GMEM_MOVEABLE, svgSz);
    if (!hg) { hr = E_OUTOFMEMORY; goto cleanup; }
    void *dst = GlobalLock(hg);
    if (dst) { CopyMemory(dst, svgBuf, svgSz); GlobalUnlock(hg); }
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &pSvgStream))) { hr = E_FAIL; goto cleanup; }
    hg = nullptr;

    float svgW = 0, svgH = 0;
    FLOAT vpW = (FLOAT)width * 2, vpH = (FLOAT)height * 2;
    GetSvgSizeFromStream(pSvgStream, &svgW, &svgH);
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
    ID2D1Device *pDevice = nullptr;
    hr = pFactory->CreateDevice(pDXGI, &pDevice);
    pDXGI->Release();
    if (FAILED(hr)) goto cleanup;
    ID2D1DeviceContext *pDC = nullptr;
    hr = pDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pDC);
    pDevice->Release();
    if (FAILED(hr)) goto cleanup;
    hr = pDC->QueryInterface(IID_PPV_ARGS(&pDC5));
    pDC->Release();
    if (FAILED(hr)) goto cleanup;

    td.Width = rtw; td.Height = rth; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = pD3D->CreateTexture2D(&td, nullptr, &pRT);
    if (FAILED(hr)) goto cleanup;
    IDXGISurface *pSurf = nullptr;
    hr = pRT->QueryInterface(IID_PPV_ARGS(&pSurf));
    if (FAILED(hr)) goto cleanup;
    hr = pDC5->CreateBitmapFromDxgiSurface(pSurf, nullptr, &pTarget);
    pSurf->Release();
    if (FAILED(hr)) goto cleanup;

    pSvgStream->Seek(lz, STREAM_SEEK_SET, nullptr);
    hr = pDC5->CreateSvgDocument(pSvgStream, D2D1::SizeF((FLOAT)rtw, (FLOAT)rth), &pSvg);
    if (FAILED(hr)) goto cleanup;

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
        pRT->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
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
        }
    }

cleanup:
    if (pCtx) pCtx->Release();
    if (pSvg) pSvg->Release();
    if (pTarget) pTarget->Release();
    if (pRT) pRT->Release();
    if (pStaging) pStaging->Release();
    if (pDC5) pDC5->Release();
    if (pFactory) pFactory->Release();
    if (pD3D) pD3D->Release();
    if (pSvgStream) pSvgStream->Release();
    if (hg) GlobalFree(hg);
    if (inlined) free(inlined);
    if (buf && buf != inlined) free(buf);
    LeaveCriticalSection(&g_cs);
    return hr;
}
