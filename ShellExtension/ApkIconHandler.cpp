#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <gdiplus.h>
#include <new>
#include <string>
#include <vector>
#include <algorithm>
#include "miniz.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")

// {1B851216-724B-4D6F-96AF-C6ACED29BDB8}
const CLSID CLSID_ApkIconHandler = 
{ 0x1b851216, 0x724b, 0x4d6f, { 0x96, 0xaf, 0xc6, 0xac, 0xed, 0x29, 0xbd, 0xb8 } };

// {E357FCCD-A995-4576-B01F-234630154E96}
const IID IID_IThumbnailProvider = 
{ 0xe357fccd, 0xa995, 0x4576, { 0xb0, 0x1f, 0x23, 0x46, 0x30, 0x15, 0x4e, 0x96 } };

long g_cRef = 0;
HINSTANCE g_hInst = NULL;

void DllAddRef() { InterlockedIncrement(&g_cRef); }
void DllRelease() { InterlockedDecrement(&g_cRef); }

class CApkIconHandler : 
    public IInitializeWithStream, 
    public IThumbnailProvider, 
    public IPersistFile, 
    public IExtractIconW,
    public IExtractIconA
{
public:
    CApkIconHandler() : _cRef(1), _pStream(NULL) { 
        _szFile[0] = 0;
        DllAddRef(); 
    }
    ~CApkIconHandler() {
        if (_pStream) _pStream->Release();
        DllRelease();
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IUnknown*>(static_cast<IThumbnailProvider*>(this));
        } else if (riid == IID_IInitializeWithStream) {
            *ppv = static_cast<IInitializeWithStream*>(this);
        } else if (riid == IID_IThumbnailProvider) {
            *ppv = static_cast<IThumbnailProvider*>(this);
        } else if (riid == IID_IPersist || riid == IID_IPersistFile) {
            *ppv = static_cast<IPersistFile*>(this);
        } else if (riid == IID_IExtractIconW) {
            *ppv = static_cast<IExtractIconW*>(this);
        } else if (riid == IID_IExtractIconA) {
            *ppv = static_cast<IExtractIconA*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
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
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HRESULT hr = E_FAIL;
        Gdiplus::Bitmap* pBitmap = NULL;
        
        if (SUCCEEDED(ExtractBitmapFromApk(&pBitmap)) && pBitmap) {
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
            delete pBitmap;
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        return hr;
    }

    // IPersist
    IFACEMETHODIMP GetClassID(CLSID *pClassID) {
        *pClassID = CLSID_ApkIconHandler;
        return S_OK;
    }

    // IPersistFile
    IFACEMETHODIMP IsDirty() { return S_FALSE; }
    IFACEMETHODIMP Load(LPCOLESTR pszFileName, DWORD dwMode) {
        wcsncpy_s(_szFile, pszFileName, MAX_PATH);
        return S_OK;
    }
    IFACEMETHODIMP Save(LPCOLESTR pszFileName, BOOL fRemember) { return E_NOTIMPL; }
    IFACEMETHODIMP SaveCompleted(LPCOLESTR pszFileName) { return E_NOTIMPL; }
    IFACEMETHODIMP GetCurFile(LPOLESTR *ppszFileName) { return E_NOTIMPL; }

    // IExtractIconW
    IFACEMETHODIMP GetIconLocation(UINT uFlags, LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) {
        if (_szFile[0] == 0 && !_pStream) return S_FALSE;
        
        if (_szFile[0] != 0) {
            wcsncpy_s(szIconFile, cchMax, _szFile, _TRUNCATE);
        } else {
            szIconFile[0] = 0;
        }
        *piIndex = 0;
        *pwFlags = GIL_PERINSTANCE; // Maintain standard compatibility with all file managers
        
        return S_OK;
    }

    IFACEMETHODIMP Extract(LPCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) {
        return ExtractIcons(phiconLarge, phiconSmall, nIconSize);
    }

    // IExtractIconA
    IFACEMETHODIMP GetIconLocation(UINT uFlags, LPSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) {
        if (_szFile[0] == 0 && !_pStream) return S_FALSE;
        
        if (_szFile[0] != 0) {
            char szFileA[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, _szFile, -1, szFileA, MAX_PATH, NULL, NULL);
            strncpy_s(szIconFile, cchMax, szFileA, _TRUNCATE);
        } else {
            szIconFile[0] = 0;
        }
        *piIndex = 0;
        *pwFlags = GIL_PERINSTANCE; // Maintain standard compatibility with all file managers
        
        return S_OK;
    }

    IFACEMETHODIMP Extract(LPCSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) {
        return ExtractIcons(phiconLarge, phiconSmall, nIconSize);
    }

private:
    struct Candidate {
        int index;
        int score;
    };

    static bool CompareCandidates(const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    }

    HRESULT ExtractIcons(HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HRESULT hr = S_FALSE;
        Gdiplus::Bitmap* pBitmap = NULL;
        
        if (SUCCEEDED(ExtractBitmapFromApk(&pBitmap)) && pBitmap) {
            int cxLarge = LOWORD(nIconSize);
            int cxSmall = HIWORD(nIconSize);
            
            bool largeSuccess = false;
            bool smallSuccess = false;

            if (phiconLarge) {
                Gdiplus::Bitmap* pLarge = (Gdiplus::Bitmap*)pBitmap->GetThumbnailImage(cxLarge, cxLarge, NULL, NULL);
                if (pLarge) {
                    if (pLarge->GetHICON(phiconLarge) == Gdiplus::Ok) {
                        largeSuccess = true;
                    }
                    delete pLarge;
                }
            }
            if (phiconSmall) {
                Gdiplus::Bitmap* pSmall = (Gdiplus::Bitmap*)pBitmap->GetThumbnailImage(cxSmall, cxSmall, NULL, NULL);
                if (pSmall) {
                    if (pSmall->GetHICON(phiconSmall) == Gdiplus::Ok) {
                        smallSuccess = true;
                    }
                    delete pSmall;
                }
            }
            
            if (largeSuccess || smallSuccess) {
                hr = S_OK;
            }
            delete pBitmap;
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        return hr;
    }

    HRESULT ExtractBitmapFromApk(Gdiplus::Bitmap** ppBitmap) {
        if (!ppBitmap) return E_POINTER;
        *ppBitmap = NULL;

        std::vector<BYTE> buffer;
        bool isStream = false;

        if (_pStream) {
            STATSTG stat;
            if (SUCCEEDED(_pStream->Stat(&stat, STATFLAG_NONAME))) {
                ULONG cbSize = stat.cbSize.LowPart;
                buffer.resize(cbSize);
                LARGE_INTEGER liZero = {0};
                _pStream->Seek(liZero, STREAM_SEEK_SET, NULL);
                ULONG cbRead = 0;
                if (SUCCEEDED(_pStream->Read(buffer.data(), cbSize, &cbRead)) && cbRead == cbSize) {
                    isStream = true;
                }
            }
        }

        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));

        if (isStream) {
            if (!mz_zip_reader_init_mem(&zip_archive, buffer.data(), buffer.size(), 0)) {
                return E_FAIL;
            }
        } else {
            if (_szFile[0] == 0) return E_FAIL;
            char szPathA[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, _szFile, -1, szPathA, MAX_PATH, NULL, NULL);
            if (!mz_zip_reader_init_file(&zip_archive, szPathA, 0)) {
                return E_FAIL;
            }
        }

        int num_files = mz_zip_reader_get_num_files(&zip_archive);
        std::vector<Candidate> candidates;

        for (int i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat file_stat;
            if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;
            if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) continue;

            std::string fname = file_stat.m_filename;
            std::string fnameLower = fname;
            std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::tolower);

            // GDI+ natively reads PNG, JPEG. We restrict to PNG and JPEGs.
            if (fnameLower.find(".png") == std::string::npos && 
                fnameLower.find(".jpg") == std::string::npos && 
                fnameLower.find(".jpeg") == std::string::npos) continue;

            int score = 0;
            if (fnameLower.find("res/") != std::string::npos) score += 10;
            if (fnameLower.find("mipmap") != std::string::npos) score += 10;
            if (fnameLower.find("drawable") != std::string::npos) score += 5;
            
            bool is_icon = false;
            if (fnameLower.find("launcher") != std::string::npos) { score += 1000; is_icon = true; }
            if (fnameLower.find("ic_launcher") != std::string::npos) { score += 1000; is_icon = true; }
            if (fnameLower.find("app_icon") != std::string::npos) { score += 800; is_icon = true; }
            if (fnameLower.find("logo") != std::string::npos) { score += 800; is_icon = true; }
            if (fnameLower.find("icon") != std::string::npos) { score += 500; is_icon = true; }
            
            if (fnameLower.find("round") != std::string::npos) score += 5;

            // Resolution priority
            if (fnameLower.find("xxxhdpi") != std::string::npos) score += 50;
            else if (fnameLower.find("xxhdpi") != std::string::npos) score += 40;
            else if (fnameLower.find("xhdpi") != std::string::npos) score += 30;
            else if (fnameLower.find("hdpi") != std::string::npos) score += 20;
            else if (fnameLower.find("mdpi") != std::string::npos) score += 10;

            if (!is_icon && (fnameLower.find("bg_") != std::string::npos || fnameLower.find("background") != std::string::npos)) {
                score -= 100; 
            }

            candidates.push_back({i, score});
        }

        std::sort(candidates.begin(), candidates.end(), CompareCandidates);

        Gdiplus::Bitmap* pBitmap = NULL;
        for (const auto& cand : candidates) {
            size_t img_size = 0;
            void *img_data = mz_zip_reader_extract_to_heap(&zip_archive, cand.index, &img_size, 0);
            if (!img_data) continue;

            IStream *pImgStream = SHCreateMemStream((const BYTE*)img_data, (UINT)img_size);
            mz_free(img_data);

            if (pImgStream) {
                pBitmap = Gdiplus::Bitmap::FromStream(pImgStream);
                if (pBitmap && pBitmap->GetLastStatus() == Gdiplus::Ok && pBitmap->GetWidth() > 0) {
                    pImgStream->Release();
                    *ppBitmap = pBitmap;
                    break;
                }
                if (pBitmap) {
                    delete pBitmap;
                    pBitmap = NULL;
                }
                pImgStream->Release();
            }
        }

        mz_zip_reader_end(&zip_archive);

        if (*ppBitmap) {
            return S_OK;
        }

        return E_FAIL;
    }

    long _cRef;
    IStream *_pStream;
    WCHAR _szFile[MAX_PATH];
};

class CApkIconHandlerClassFactory : public IClassFactory
{
public:
    CApkIconHandlerClassFactory() : _cRef(1) { DllAddRef(); }
    ~CApkIconHandlerClassFactory() { DllRelease(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        static const QITAB qit[] = {
            QITABENT(CApkIconHandlerClassFactory, IClassFactory),
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
        CApkIconHandler *pProvider = new (std::nothrow) CApkIconHandler();
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
    if (rclsid == CLSID_ApkIconHandler) {
        CApkIconHandlerClassFactory *pCF = new (std::nothrow) CApkIconHandlerClassFactory();
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
