#include "thumbprovider.h"
#include <strsafe.h>

const CLSID CLSID_SvgThumbnailProvider =
    { 0x9b2a1c9a, 0x9e5d, 0x4b5a, { 0x8f, 0x3a, 0x5c, 0x6e, 0x8f, 0x1a, 0x2b, 0x3d } };

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
    if (!IsEqualCLSID(rclsid, CLSID_SvgThumbnailProvider))
        return CLASS_E_CLASSNOTAVAILABLE;

    CThumbProviderClassFactory *pFactory = new (std::nothrow) CThumbProviderClassFactory();
    if (!pFactory)
        return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

HRESULT __stdcall DllCanUnloadNow()
{
    return g_cLock > 0 ? S_FALSE : S_OK;
}

HRESULT __stdcall DllRegisterServer()
{
    HKEY hKey;
    WCHAR szModulePath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, szModulePath, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    LPCWSTR clsid = L"{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}";
    WCHAR keyPath[MAX_PATH];

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s", clsid);
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"SVG Thumbnail Provider", 42);
        RegCloseKey(hKey);
    }

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s\\InProcServer32", clsid);
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
        RegSetValueExW(hKey, clsid, 0, REG_SZ, (BYTE*)L"SVG Thumbnail Provider", 46);
        RegCloseKey(hKey);
    }

    WCHAR thumbHandler[] = L".svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, thumbHandler, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }

    WCHAR sysAssoc[] = L"SystemFileAssociations\\.svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
    if (SUCCEEDED(HRESULT_FROM_WIN32(RegCreateKeyExW(HKEY_CLASSES_ROOT, sysAssoc, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr))))
    {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)clsid, (DWORD)((wcslen(clsid) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);
    }

    return S_OK;
}

HRESULT __stdcall DllUnregisterServer()
{
    LPCWSTR clsid = L"{9B2A1C9A-9E5D-4B5A-8F3A-5C6E8F1A2B3D}";
    WCHAR keyPath[MAX_PATH];

    StringCchPrintfW(keyPath, MAX_PATH, L"CLSID\\%s", clsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);

    RegDeleteTreeW(HKEY_CLASSES_ROOT, L".svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"SystemFileAssociations\\.svg\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}");

    HKEY hKey;
    if (SUCCEEDED(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", 0, KEY_WRITE, &hKey)))
    {
        RegDeleteValueW(hKey, clsid);
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

    HRESULT hr = RenderWithDirect2D(cx, phbmp);
    if (FAILED(hr))
        hr = RenderFallback(cx, phbmp);

    if (SUCCEEDED(hr))
        *pdwAlpha = WTSAT_RGB;

    return hr;
}

HRESULT CSvgThumbnailProvider::RenderWithDirect2D(UINT cx, HBITMAP *phbmp)
{
    HRESULT hr;

    ID3D11Device *pD3D = nullptr;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &pD3D, nullptr, nullptr);
    if (FAILED(hr)) return hr;

    IDXGIDevice *pDXGI = nullptr;
    hr = pD3D->QueryInterface(IID_PPV_ARGS(&pDXGI));
    if (FAILED(hr)) { pD3D->Release(); return hr; }

    ID2D1Factory5 *pFactory = nullptr;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) { pDXGI->Release(); pD3D->Release(); return hr; }

    ID2D1Device *pDevice = nullptr;
    hr = pFactory->CreateDevice(pDXGI, &pDevice);
    if (FAILED(hr)) { pFactory->Release(); pDXGI->Release(); pD3D->Release(); return hr; }

    ID2D1DeviceContext *pDC = nullptr;
    hr = pDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pDC);
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); return hr; }

    ID2D1DeviceContext5 *pDC5 = nullptr;
    hr = pDC->QueryInterface(IID_PPV_ARGS(&pDC5));
    pDC->Release();
    if (FAILED(hr)) { pDevice->Release(); pFactory->Release(); pDXGI->Release(); pD3D->Release(); return hr; }

    pDevice->Release();
    pDXGI->Release();

    LARGE_INTEGER liZero = {};
    m_pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);

    D2D1_SIZE_F viewport = D2D1::SizeF((FLOAT)cx, (FLOAT)cx);
    ID2D1SvgDocument *pSvg = nullptr;
    hr = pDC5->CreateSvgDocument(m_pStream, viewport, &pSvg);
    if (FAILED(hr)) { pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    UINT bmpW = (UINT)max(1, (INT)viewport.width);
    UINT bmpH = (UINT)max(1, (INT)viewport.height);

    ID2D1Bitmap1 *pTarget = nullptr;
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    hr = pDC5->CreateBitmap(D2D1::SizeU(bmpW, bmpH), nullptr, 0, &bp, &pTarget);
    if (FAILED(hr)) { pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    pDC5->SetTarget(pTarget);

    pDC5->BeginDraw();
    pDC5->Clear(D2D1::ColorF(D2D1::ColorF::White, 1.0f));
    pDC5->DrawSvgDocument(pSvg);
    hr = pDC5->EndDraw();
    if (FAILED(hr)) { pTarget->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    IDXGISurface *pSurface = nullptr;
    hr = pTarget->QueryInterface(&pSurface);
    if (FAILED(hr)) { pTarget->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    ID3D11Texture2D *pBackTex = nullptr;
    hr = pSurface->QueryInterface(&pBackTex);
    pSurface->Release();
    if (FAILED(hr)) { pTarget->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    D3D11_TEXTURE2D_DESC texDesc = {};
    pBackTex->GetDesc(&texDesc);
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texDesc.MiscFlags = 0;

    ID3D11Texture2D *pStagingTex = nullptr;
    hr = pD3D->CreateTexture2D(&texDesc, nullptr, &pStagingTex);
    if (FAILED(hr)) { pBackTex->Release(); pTarget->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    ID3D11DeviceContext *pCtx = nullptr;
    pD3D->GetImmediateContext(&pCtx);
    pCtx->CopyResource(pStagingTex, pBackTex);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = pCtx->Map(pStagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) { pCtx->Release(); pStagingTex->Release(); pBackTex->Release(); pTarget->Release(); pSvg->Release(); pDC5->Release(); pFactory->Release(); pD3D->Release(); return hr; }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)bmpW;
    bmi.bmiHeader.biHeight = -(LONG)bmpH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hBmp && bits)
    {
        for (UINT y = 0; y < bmpH; y++)
            CopyMemory((BYTE*)bits + y * bmpW * 4, (BYTE*)mapped.pData + y * mapped.RowPitch, bmpW * 4);
        *phbmp = hBmp;
        hr = S_OK;
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }

    pCtx->Unmap(pStagingTex, 0);
    pCtx->Release();
    pStagingTex->Release();
    pBackTex->Release();
    pTarget->Release();
    pSvg->Release();
    pDC5->Release();
    pFactory->Release();
    pD3D->Release();

    return hr;
}

HRESULT CSvgThumbnailProvider::RenderFallback(UINT cx, HBITMAP *phbmp)
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

    SelectObject(hMemDC, hOldBmp);
    DeleteDC(hMemDC);

    *phbmp = hBmp;
    return S_OK;
}
