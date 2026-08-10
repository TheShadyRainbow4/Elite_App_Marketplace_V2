#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <gdiplus.h>
#include <new>
#include <string>
#include <vector>
#include <algorithm>
#include "miniz.h"

// CLSID for our APK Thumbnail Provider
// {1B851216-724B-4D6F-96AF-C6ACED29BDB8}
const CLSID CLSID_ApkThumbnailProvider = 
{ 0x1b851216, 0x724b, 0x4d6f, { 0x96, 0xaf, 0xc6, 0xac, 0xed, 0x29, 0xbd, 0xb8 } };

long g_cRef = 0;
HINSTANCE g_hInst = NULL;

void DllAddRef() { InterlockedIncrement(&g_cRef); }
void DllRelease() { InterlockedDecrement(&g_cRef); }

class CApkThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider
{
public:
    CApkThumbnailProvider() : _cRef(1), _pStream(NULL) { DllAddRef(); }
    ~CApkThumbnailProvider() {
        if (_pStream) _pStream->Release();
        DllRelease();
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        static const QITAB qit[] = {
            QITABENT(CApkThumbnailProvider, IInitializeWithStream),
            QITABENT(CApkThumbnailProvider, IThumbnailProvider),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG cRef = InterlockedDecrement(&_cRef);
        if (0 == cRef) delete this;
        return cRef;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pStream, DWORD grfMode) {
        if (_pStream) {
            _pStream->Release();
            _pStream = NULL;
        }
        _pStream = pStream;
        if (_pStream) _pStream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) {
        if (!_pStream) return E_UNEXPECTED;

        // Read stream into memory
        STATSTG stat;
        if (FAILED(_pStream->Stat(&stat, STATFLAG_NONAME))) return E_FAIL;
        
        ULONG cbSize = stat.cbSize.LowPart;
        std::vector<BYTE> buffer(cbSize);
        ULONG cbRead = 0;
        
        LARGE_INTEGER liZero = {0};
        _pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
        if (FAILED(_pStream->Read(buffer.data(), cbSize, &cbRead)) || cbRead != cbSize) {
            return E_FAIL;
        }

        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));
        if (!mz_zip_reader_init_mem(&zip_archive, buffer.data(), cbSize, 0)) {
            return E_FAIL;
        }

        int num_files = mz_zip_reader_get_num_files(&zip_archive);
        int best_idx = -1;
        int best_score = -1;

        for (int i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat file_stat;
            if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
            if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) continue;

            std::string fname = file_stat.m_filename;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (fname.find(".png") == std::string::npos) continue;

            int score = 0;
            if (fname.find("res/") != std::string::npos) score += 10;
            if (fname.find("mipmap") != std::string::npos) score += 10;
            if (fname.find("drawable") != std::string::npos) score += 5;
            if (fname.find("launcher") != std::string::npos) score += 30;
            if (fname.find("icon") != std::string::npos) score += 20;

            if (fname.find("xxxhdpi") != std::string::npos) score += 50;
            else if (fname.find("xxhdpi") != std::string::npos) score += 40;
            else if (fname.find("xhdpi") != std::string::npos) score += 30;
            else if (fname.find("hdpi") != std::string::npos) score += 20;
            else if (fname.find("mdpi") != std::string::npos) score += 10;
            
            if (fname.find("round") != std::string::npos) score += 5;

            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            mz_zip_reader_end(&zip_archive);
            return E_FAIL;
        }

        size_t png_size = 0;
        void *png_data = mz_zip_reader_extract_to_heap(&zip_archive, best_idx, &png_size, 0);
        mz_zip_reader_end(&zip_archive);

        if (!png_data) return E_FAIL;

        IStream *pPngStream = SHCreateMemStream((const BYTE*)png_data, (UINT)png_size);
        mz_free(png_data);

        if (!pPngStream) return E_OUTOFMEMORY;

        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HRESULT hr = E_FAIL;
        {
            Gdiplus::Bitmap *pBitmap = Gdiplus::Bitmap::FromStream(pPngStream);
            if (pBitmap && pBitmap->GetLastStatus() == Gdiplus::Ok) {
                // Optional: We can scale to 'cx' but Explorer often scales it if needed.
                // CreateDIBSection or GetHBITMAP?
                // GDI+ GetHBITMAP loses true alpha (creates premultiplied with a background color).
                // Actually GetHBITMAP doesn't preserve the alpha channel correctly for icons.
                // We must create an ARGB DIB.
                // However, Gdiplus::Bitmap::GetHBITMAP with transparent color sometimes works.
                // A better approach is copying bits manually, but let's try GetHBITMAP first.
                // Or maybe draw it to a 32-bpp DIB section.

                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
                bmi.bmiHeader.biWidth = pBitmap->GetWidth();
                bmi.bmiHeader.biHeight = -(INT)pBitmap->GetHeight(); // top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                void* pBits = NULL;
                HDC hdc = GetDC(NULL);
                HBITMAP hDIB = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                ReleaseDC(NULL, hdc);

                if (hDIB) {
                    Gdiplus::BitmapData bitmapData;
                    Gdiplus::Rect rect(0, 0, pBitmap->GetWidth(), pBitmap->GetHeight());
                    if (pBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
                        for (UINT y = 0; y < pBitmap->GetHeight(); ++y) {
                            BYTE* pDstLine = (BYTE*)pBits + y * bmi.bmiHeader.biWidth * 4;
                            BYTE* pSrcLine = (BYTE*)bitmapData.Scan0 + y * bitmapData.Stride;
                            memcpy(pDstLine, pSrcLine, pBitmap->GetWidth() * 4);
                        }
                        pBitmap->UnlockBits(&bitmapData);
                        
                        *phbmp = hDIB;
                        if (pdwAlpha) *pdwAlpha = WTSAT_ARGB;
                        hr = S_OK;
                    } else {
                        DeleteObject(hDIB);
                    }
                }
            }
            if (pBitmap) delete pBitmap;
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        pPngStream->Release();

        return hr;
    }

private:
    long _cRef;
    IStream *_pStream;
};

class CApkThumbnailProviderClassFactory : public IClassFactory
{
public:
    CApkThumbnailProviderClassFactory() : _cRef(1) { DllAddRef(); }
    ~CApkThumbnailProviderClassFactory() { DllRelease(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        static const QITAB qit[] = {
            QITABENT(CApkThumbnailProviderClassFactory, IClassFactory),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_cRef); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG cRef = InterlockedDecrement(&_cRef);
        if (0 == cRef) delete this;
        return cRef;
    }

    IFACEMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CApkThumbnailProvider *pProvider = new (std::nothrow) CApkThumbnailProvider();
        if (!pProvider) return E_OUTOFMEMORY;
        HRESULT hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock) {
        if (fLock) DllAddRef();
        else DllRelease();
        return S_OK;
    }

private:
    long _cRef;
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (rclsid == CLSID_ApkThumbnailProvider) {
        CApkThumbnailProviderClassFactory *pCF = new (std::nothrow) CApkThumbnailProviderClassFactory();
        if (!pCF) return E_OUTOFMEMORY;
        HRESULT hr = pCF->QueryInterface(riid, ppv);
        pCF->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow()
{
    return g_cRef == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
    }
    return TRUE;
}
