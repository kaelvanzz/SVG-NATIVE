#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <shlwapi.h>
#include <new>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11.h>
#include <wincodec.h>

#define DLL_EXPORT extern "C" __declspec(dllexport)

class CThumbProviderClassFactory : public IClassFactory
{
public:
    CThumbProviderClassFactory() : m_cRef(1) {}
    virtual ~CThumbProviderClassFactory() {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CThumbProviderClassFactory, IClassFactory),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv);
    IFACEMETHODIMP LockServer(BOOL fLock) { return S_OK; }

private:
    LONG m_cRef;
};

class CSvgThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider
{
public:
    CSvgThumbnailProvider() : m_cRef(1), m_pStream(nullptr) {}
    virtual ~CSvgThumbnailProvider()
    {
        if (m_pStream) m_pStream->Release();
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CSvgThumbnailProvider, IInitializeWithStream),
            QITABENT(CSvgThumbnailProvider, IThumbnailProvider),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP Initialize(IStream *pStream, DWORD grfMode);
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha);

private:
    HRESULT RenderWithDirect2D(UINT cx, HBITMAP *phbmp);
    HRESULT RenderFallback(UINT cx, HBITMAP *phbmp);

    LONG m_cRef;
    IStream *m_pStream;
};
