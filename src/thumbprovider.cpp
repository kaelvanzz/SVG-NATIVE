#include "thumbprovider.h"
#include <strsafe.h>

const CLSID CLSID_SvgThumbnailProvider =
    { 0x9b2a1c9a, 0x9e5d, 0x4b5a, { 0x8f, 0x3a, 0x5c, 0x6e, 0x8f, 0x1a, 0x2b, 0x3d } };
const CLSID CLSID_SvgWicDecoder =
    { 0x11e7785d, 0x7bfe, 0x411c, { 0xad, 0x88, 0x48, 0x84, 0x9c, 0x9e, 0xe8, 0xb1 } };

static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz);
static HRESULT GetSvgSizeFromBuffer(const char *buf, ULONG sz, float *pW, float *pH);
static HRESULT GetSvgSizeFromStream(IStream *pStream, float *pW, float *pH);
static HRESULT RenderSvgToBuffer(IStream *pStream, UINT width, UINT height, UINT stride, BYTE *pixels);
static HRESULT CreateWicDecoderInstance(REFIID riid, void **ppv);

static HINSTANCE g_hInst;
static long g_cLock;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    g_hInst = hModule;
    return TRUE;
}

HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    *ppv = nullptr;
    if (IsEqualCLSID(rclsid, CLSID_SvgThumbnailProvider))
    {
        CThumbProviderClassFactory *pFactory = new (std::nothrow) CThumbProviderClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    if (IsEqualCLSID(rclsid, CLSID_SvgWicDecoder))
        return CreateWicDecoderInstance(riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT __stdcall DllCanUnloadNow()
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

    // Clean up old Patterns key (removed in v2 - WIC discovery was breaking on it)
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Patterns", clsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);

    // CLSID
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

    // InProcServer32
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\InProcServer32", clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        DWORD len = (DWORD)((wcslen(modulePath) + 1) * sizeof(WCHAR));
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)modulePath, len);
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Both", 10);
        RegCloseKey(hKey);
    }

    // Categories - mark as WIC Bitmap Decoder
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Categories\\%s", clsid, cat);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        RegCloseKey(hKey);

    // Instance - required for WIC enumeration
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Instance\\%s", cat, clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"FriendlyName", 0, REG_SZ, (BYTE*)L"SVG WIC Decoder", 30);
        RegCloseKey(hKey);
    }

    // Formats - 32bppBGRA output format
    LPCWSTR fmt32 = L"{6FDDC324-4E03-4BFE-B185-3D77768DC90D}";
    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\Formats\\%s", clsid, fmt32);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
        RegCloseKey(hKey);

    // MIME database mapping: make image/svg+xml point to our decoder
    StringCchPrintfW(keyPath, MAX_PATH, L"MIME\\Database\\Content Type\\image/svg+xml");
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, L"CLSID", 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"Extension", 0, REG_SZ, (BYTE*)L".svg", 10);
        RegCloseKey(hKey);
    }
}

static void UnregWicDecoder()
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{7ED96837-96F0-4812-B211-F13C24117ED3}\\Instance\\{11E7785D-7BFE-411C-AD88-48849C9EE8B1}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"MIME\\Database\\Content Type\\image/svg+xml");
}

HRESULT __stdcall DllRegisterServer()
{
    HKEY hKey;
    WCHAR szModulePath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, szModulePath, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    LPCWSTR tpClsid = L"{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}";
    WCHAR keyPath[MAX_PATH];

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s", tpClsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SVG Thumbnail Provider", 42);
        RegCloseKey(hKey);
    }

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\InProcServer32", tpClsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        DWORD len = (DWORD)((wcslen(szModulePath) + 1) * sizeof(WCHAR));
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)szModulePath, len);
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment", 20);
        RegCloseKey(hKey);
    }

    WCHAR approvedPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_LOCAL_MACHINE, approvedPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, tpClsid, 0, REG_SZ, (BYTE*)L"SVG Thumbnail Provider", 46);
        RegCloseKey(hKey);
    }

    WCHAR thumbHandler[] = L".svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, thumbHandler, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)tpClsid, (DWORD)((wcslen(tpClsid) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }

    WCHAR sysAssoc[] = L"SystemFileAssociations\\.svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, sysAssoc, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)tpClsid, (DWORD)((wcslen(tpClsid) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }

    RegWicDecoder(szModulePath);

    return S_OK;
}

HRESULT __stdcall DllUnregisterServer()
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L".svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"SystemFileAssociations\\.svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}");

    UnregWicDecoder();

    HKEY hKey;
    if (SUCCEEDED(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_WRITE, &hKey)))
    {
        RegDeleteValueW(hKey, L"{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}");
        RegCloseKey(hKey);
    }

    return S_OK;
}

IFACEMETHODIMP CThumbProviderClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv)
{
    *ppv = nullptr;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    CSvgThumbnailProvider *pProvider = new (std::nothrow) CSvgThumbnailProvider();
    if (!pProvider)
        return E_OUTOFMEMORY;

    HRESULT hr = pProvider->QueryInterface(riid, ppv);
    pProvider->Release();
    return hr;
}

IFACEMETHODIMP CSvgThumbnailProvider::Initialize(IStream *pStream, DWORD grfMode)
{
    if (m_pStream)
    {
        m_pStream->Release();
        m_pStream = nullptr;
    }
    m_pStream = pStream;
    m_pStream->AddRef();
    return S_OK;
}

IFACEMETHODIMP CSvgThumbnailProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_UNKNOWN;

    if (!m_pStream)
        return E_FAIL;

    HRESULT hr;
    DWORD dwErr = 0;

    // D2D first (fast, handles most SVGs with inline styles)
    hr = RenderWithDirect2D(cx, phbmp);
    if (FAILED(hr))
    {
        dwErr = hr;
        hr = RenderWithWic(cx, phbmp);
    }
    if (FAILED(hr))
    {
        hr = RenderFallback(cx, phbmp, dwErr);
    }

    if (SUCCEEDED(hr))
        *pdwAlpha = WTSAT_ARGB;

    return hr;
}

HRESULT CSvgThumbnailProvider::RenderWithDirect2D(UINT cx, HBITMAP *phbmp)
{
    HRESULT hr;

    ID3D11Device *pD3D = nullptr;
    // Try WARP first (software, most compatible)
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &pD3D, nullptr, nullptr);
    if (FAILED(hr))
    {
        // Fall back to hardware if WARP fails (unlikely)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &pD3D, nullptr, nullptr);
    }
    if (FAILED(hr)) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x201);

    IDXGIDevice *pDXGI = nullptr;
    hr = pD3D->QueryInterface(IID_PPV_ARGS(&pDXGI));
    if (FAILED(hr)) { pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x202); }

    ID2D1Factory5 *pFactory = nullptr;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { pDXGI->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x203); }

    ID2D1Device *pDevice = nullptr;
    hr = pFactory->CreateDevice(pDXGI, &pDevice);
    if (FAILED(hr)) { pFactory->Release(); pDXGI->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x204); }

    ID2D1DeviceContext *pDC = nullptr;
    hr = pDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pDC);
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x205); }

    ID2D1DeviceContext5 *pDC5 = nullptr;
    hr = pDC->QueryInterface(IID_PPV_ARGS(&pDC5));
    pDC->Release();
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x206); }

    pDevice->Release();
    pDXGI->Release();

    // Read entire stream into a buffer, inline CSS, create new memory stream
    // (Explorer's stream may be read-only, so we work with a copy)
    IStream *pSvgStream = nullptr;
    float svgW = 0, svgH = 0;
    FLOAT vpW = (FLOAT)cx, vpH = (FLOAT)cx;
    {
        STATSTG stat = {};
        hr = m_pStream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x210); }
        ULONG sz = (ULONG)stat.cbSize.QuadPart;
        if (sz == 0 || sz > 4194304) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x211); }

        char *buf = (char*)malloc(sz + 1);
        if (!buf) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return E_OUTOFMEMORY; }
        LARGE_INTEGER z = {};
        m_pStream->Seek(z, STREAM_SEEK_SET, nullptr);
        ULONG r = 0;
        m_pStream->Read(buf, sz, &r);
        buf[sz] = 0;

        // Try to inline CSS; if it fails, use original buffer
        char *svgBuf = buf;
        ULONG svgSz = sz;
        char *inlined = nullptr;
        if (strstr(buf, "<style") && SUCCEEDED(InlineSvgCss(buf, sz, &inlined, &svgSz)) && inlined)
        {
            free(buf);
            svgBuf = inlined;
        }

        GetSvgSizeFromBuffer(svgBuf, svgSz, &svgW, &svgH);
        if (svgW > 0 && svgH > 0) { vpW = svgW; vpH = svgH; }
        // Minimum viewport for sharp downscale (e.g. BTC width="100%"→100)
        vpW = max(vpW, (FLOAT)cx * 2);
        vpH = max(vpH, (FLOAT)cx * 2);

        // Create memory stream from the (possibly inlined) buffer
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, svgSz);
        if (hg)
        {
            void *dst = GlobalLock(hg);
            if (dst) { CopyMemory(dst, svgBuf, svgSz); GlobalUnlock(hg); }
            CreateStreamOnHGlobal(hg, TRUE, &pSvgStream);
        }
        if (svgBuf != buf) free(svgBuf);
        else free(buf);
    }
    if (!pSvgStream) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x212); }

    ID2D1SvgDocument *pSvg = nullptr;
    hr = pDC5->CreateSvgDocument(pSvgStream, D2D1::SizeF(vpW, vpH), &pSvg);
    pSvgStream->Release();
    if (FAILED(hr)) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x213); }

    // Create render target at full viewport size to avoid clipping
    // D2D clips to the render target bounds regardless of viewport
    UINT rtw = (UINT)min((FLOAT)2048, vpW);
    UINT rth = (UINT)min((FLOAT)2048, vpH);
    if (rtw < 1) rtw = 1; if (rth < 1) rth = 1;

    ID3D11Texture2D *pRenderTex = nullptr;
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = rtw;
        td.Height = rth;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = pD3D->CreateTexture2D(&td, nullptr, &pRenderTex);
    }
    if (FAILED(hr)) { pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x208); }

    IDXGISurface *pSurface = nullptr;
    hr = pRenderTex->QueryInterface(IID_PPV_ARGS(&pSurface));
    if (FAILED(hr)) { pRenderTex->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x20A); }

    ID2D1Bitmap1 *pTarget = nullptr;
    hr = pDC5->CreateBitmapFromDxgiSurface(pSurface, nullptr, &pTarget);
    pSurface->Release();
    pSurface = nullptr;
    if (FAILED(hr)) { pRenderTex->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x20B); }

    ID3D11Texture2D *pStagingTex = nullptr;
    ID3D11DeviceContext *pCtx = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HBITMAP hBmp = nullptr;
    void *bits = nullptr;
    hr = S_OK;

    __try
    {
        pDC5->SetTarget(pTarget);
        pDC5->BeginDraw();
        pDC5->Clear(D2D1::ColorF(0, 0, 0, 0));
        pDC5->DrawSvgDocument(pSvg);
        hr = pDC5->EndDraw();
        if (FAILED(hr)) { hr = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x209); goto cleanup; }

        D3D11_TEXTURE2D_DESC texDesc = {};
        pRenderTex->GetDesc(&texDesc);
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.BindFlags = 0;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texDesc.MiscFlags = 0;

        hr = pD3D->CreateTexture2D(&texDesc, nullptr, &pStagingTex);
        if (FAILED(hr)) { hr = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x20C); goto cleanup; }

        pD3D->GetImmediateContext(&pCtx);
        pCtx->CopyResource(pStagingTex, pRenderTex);

        hr = pCtx->Map(pStagingTex, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) { hr = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x20D); goto cleanup; }

        // Scale down to cx x cx HBITMAP using bilinear interpolation
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)cx;
        bmi.bmiHeader.biHeight = -(LONG)cx;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hBmp && bits)
        {
            ZeroMemory(bits, (SIZE_T)cx * cx * 4);
            // Center and bilinear-scale from mapped buffer to cx x cx
            BYTE *src = (BYTE*)mapped.pData;
            UINT sw = rtw, sh = rth;
            FLOAT sx = (FLOAT)sw / (FLOAT)cx;
            FLOAT sy = (FLOAT)sh / (FLOAT)cx;
            FLOAT sxf = max(sx, sy);
            FLOAT syf = sxf;
            int srcW = (int)(cx * (sw / (FLOAT)max(sw, sh)));
            int srcH = (int)(cx * (sh / (FLOAT)max(sw, sh)));
            int xOff = ((int)cx - srcW) / 2;
            int yOff = ((int)cx - srcH) / 2;
            FLOAT stepX = (FLOAT)sw / (FLOAT)srcW;
            FLOAT stepY = (FLOAT)sh / (FLOAT)srcH;
            for (int y = 0; y < srcH; y++)
            {
                FLOAT srcY = y * stepY;
                int iy0 = (int)srcY;
                int iy1 = min(iy0 + 1, (int)sh - 1);
                FLOAT fy = srcY - iy0;
                BYTE *dstLine = (BYTE*)bits + (yOff + y) * cx * 4 + xOff * 4;
                for (int x = 0; x < srcW; x++)
                {
                    FLOAT srcX = x * stepX;
                    int ix0 = (int)srcX;
                    int ix1 = min(ix0 + 1, (int)sw - 1);
                    FLOAT fx = srcX - ix0;
                    for (int c = 0; c < 4; c++)
                    {
                        FLOAT v = (1-fy)*((1-fx)*src[iy0*mapped.RowPitch+ix0*4+c] + fx*src[iy0*mapped.RowPitch+ix1*4+c])
                                + fy *((1-fx)*src[iy1*mapped.RowPitch+ix0*4+c] + fx*src[iy1*mapped.RowPitch+ix1*4+c]);
                        dstLine[x*4+c] = (BYTE)(v + 0.5f);
                    }
                }
            }
            *phbmp = hBmp;
            hr = S_OK;
        }
        else
        {
            hr = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x20E);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        hr = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x2FF);
    }

cleanup:
    if (pCtx && mapped.pData) pCtx->Unmap(pStagingTex, 0);
    if (pCtx) pCtx->Release();
    if (pStagingTex) pStagingTex->Release();
    if (pTarget) pTarget->Release();
    if (pRenderTex) pRenderTex->Release();
    if (pSvg) pSvg->Release();
    if (pDC5) pDC5->Release();
    if (pFactory) pFactory->Release();
    if (pD3D) pD3D->Release();

    return hr;
}

// Extracts width/height from <svg> tag in a buffer.
// Returns S_OK with parsed values, S_FALSE if not found.
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
    if (w) sscanf_s(w + 7, "%f", &fw);
    if (h) sscanf_s(h + 8, "%f", &fh);
    free(tag);
    if (fw > 0 && fh > 0) { *pW = fw; *pH = fh; return S_OK; }
    return S_FALSE;
}

// Inlines CSS classes from <style> blocks into inline fill/stroke attributes.
// Takes input buffer + size, allocates output buffer with inlined CSS.
static HRESULT InlineSvgCss(const char *in, ULONG inSz, char **out, ULONG *outSz)
{
    *out = nullptr;
    if (!strstr(in, "<style")) return S_FALSE;

    char *buf = (char*)malloc(inSz + 1);
    if (!buf) return E_OUTOFMEMORY;
    memcpy(buf, in, inSz);
    buf[inSz] = 0;

    // Parse CSS rules FIRST (before erasing style blocks)
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
        // Erase the <style> block AFTER parsing
        memset(p, ' ', endStyle + 8 - p);
        p = endStyle + 8;
    }
    if (nRules == 0) { free(buf); return S_FALSE; }

    // Build output: copy buf with inlined attributes
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

        // Handle self-closing tags: insert fill/stroke BEFORE '/>'
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

HRESULT CSvgThumbnailProvider::RenderWithWic(UINT cx, HBITMAP *phbmp)
{
    LARGE_INTEGER liZero = {};
    m_pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);

    HRESULT hr;
    IWICImagingFactory *pWicFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWicFactory));
    if (FAILED(hr)) return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x301);

    // Try automatic decoder detection first (works for .svg if registered)
    IWICBitmapDecoder *pDecoder = nullptr;
    hr = pWicFactory->CreateDecoderFromStream(m_pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr))
    {
        // CLSID_WICSvgDecoder: {DD0C5C12-3E5B-45C3-AB4B-2A08D0B4F9A0}
        static const GUID CLSID_SvgDecoder =
            { 0xdd0c5c12, 0x3e5b, 0x45c3, { 0xab, 0x4b, 0x2a, 0x8, 0xd0, 0xb4, 0xf9, 0xa0 } };
        m_pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);
        hr = pWicFactory->CreateDecoder(CLSID_SvgDecoder, nullptr, &pDecoder);
        if (FAILED(hr)) { pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x302); }
        m_pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);
        hr = pDecoder->Initialize(m_pStream, WICDecodeMetadataCacheOnDemand);
        if (FAILED(hr)) { pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x303); }
    }

    IWICBitmapFrameDecode *pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) { pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x304); }

    UINT fw, fh;
    pFrame->GetSize(&fw, &fh);
    if (fw == 0 || fh == 0) { pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x305); }

    IWICBitmapScaler *pScaler = nullptr;
    hr = pWicFactory->CreateBitmapScaler(&pScaler);
    if (FAILED(hr)) { pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x306); }
    double scale = min((double)cx / fw, (double)cx / fh);
    UINT scaledW = (UINT)max(1, (INT)(fw * scale));
    UINT scaledH = (UINT)max(1, (INT)(fh * scale));
    hr = pScaler->Initialize(pFrame, scaledW, scaledH, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) { pScaler->Release(); pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x307); }

    IWICFormatConverter *pConverter = nullptr;
    hr = pWicFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) { pScaler->Release(); pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x308); }
    hr = pConverter->Initialize(pScaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { pConverter->Release(); pScaler->Release(); pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x309); }

    // Create final bitmap at cx x cx with transparent background
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)cx;
    bmi.bmiHeader.biHeight = -(LONG)cx;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) { pConverter->Release(); pScaler->Release(); pFrame->Release(); pDecoder->Release(); pWicFactory->Release(); return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x30A); }
    // Zero-fill for transparent background
    ZeroMemory(bits, (SIZE_T)cx * cx * 4);

    // Copy scaled SVG pixels into center
    {
        UINT srcStride = scaledW * 4;
        UINT bufSize = srcStride * scaledH;
        BYTE *srcBits = (BYTE*)malloc(bufSize);
        if (srcBits)
        {
            hr = pConverter->CopyPixels(nullptr, srcStride, bufSize, srcBits);
            if (SUCCEEDED(hr))
            {
                int xOff = ((int)cx - (int)scaledW) / 2;
                int yOff = ((int)cx - (int)scaledH) / 2;
                for (UINT y = 0; y < scaledH; y++)
                {
                    CopyMemory((BYTE*)bits + (yOff + y) * cx * 4 + xOff * 4,
                               srcBits + y * srcStride, srcStride);
                }
            }
            free(srcBits);
        }
    }

    pConverter->Release();
    pScaler->Release();
    pFrame->Release();
    pDecoder->Release();
    pWicFactory->Release();

    *phbmp = hBmp;
    return S_OK;
}

HRESULT CSvgThumbnailProvider::RenderFallback(UINT cx, HBITMAP *phbmp, DWORD dwError)
{
    HDC hDC = GetDC(nullptr);
    if (!hDC) return E_FAIL;

    HDC hMemDC = CreateCompatibleDC(hDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hDC, (int)cx, (int)cx);
    ReleaseDC(nullptr, hDC);

    if (!hBmp || !hMemDC)
    {
        if (hBmp) DeleteObject(hBmp);
        if (hMemDC) DeleteDC(hMemDC);
        return E_FAIL;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);

    RECT rect = { 0, 0, (int)cx, (int)cx };
    HBRUSH hBrush = CreateSolidBrush(RGB(248, 248, 250));
    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 215));
    SelectObject(hMemDC, hPen);
    SelectObject(hMemDC, GetStockObject(NULL_BRUSH));
    Rectangle(hMemDC, 8, 8, (int)cx - 8, (int)cx - 8);
    DeleteObject(hPen);

    SetBkMode(hMemDC, TRANSPARENT);
    SetTextColor(hMemDC, RGB(140, 140, 160));
    HFONT hFont = CreateFontA((int)(cx * 0.22), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    SelectObject(hMemDC, hFont);
    RECT textRect;
    CopyRect(&textRect, &rect);
    InflateRect(&textRect, -12, -12);
    DrawTextA(hMemDC, "SVG", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(hFont);

    if (dwError)
    {
        CHAR errBuf[32];
        StringCchPrintfA(errBuf, 32, "err=%08X", dwError);
        HFONT hSmallFont = CreateFontA((int)(cx * 0.07), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        SelectObject(hMemDC, hSmallFont);
        SetTextColor(hMemDC, RGB(200, 50, 50));
        RECT errRect;
        CopyRect(&errRect, &rect);
        InflateRect(&errRect, -12, -12);
        OffsetRect(&errRect, 0, (int)(cx * 0.25));
        DrawTextA(hMemDC, errBuf, -1, &errRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        DeleteObject(hSmallFont);
    }

    SelectObject(hMemDC, hOldBmp);
    DeleteDC(hMemDC);

    *phbmp = hBmp;
    return S_OK;
}

// ──── WIC SVG Decoder ──────────────────────────────────────────

static const GUID GUID_ContainerFormatSvg =
    { 0xa6ba1b82, 0x2489, 0x4b33, { 0x9f, 0x3a, 0xca, 0x6b, 0x5c, 0x3a, 0x9a, 0x4b } };

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

static HRESULT CreateWicDecoderInstance(REFIID riid, void **ppv)
{
    CSvgWicDecoderFactory *pF = new (std::nothrow) CSvgWicDecoderFactory();
    if (!pF) return E_OUTOFMEMORY;
    HRESULT hr = pF->QueryInterface(riid, ppv);
    pF->Release();
    return hr;
}

// ──── RenderSvgToBuffer ─────────────────────────────────────────

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
