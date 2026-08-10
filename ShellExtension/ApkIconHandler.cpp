#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
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

long g_cRef = 0;
HINSTANCE g_hInst = NULL;

void DllAddRef() { InterlockedIncrement(&g_cRef); }
void DllRelease() { InterlockedDecrement(&g_cRef); }

class CApkIconHandler : public IPersistFile, public IExtractIconW
{
public:
    CApkIconHandler() : _cRef(1) { 
        _szFile[0] = 0;
        DllAddRef(); 
    }
    ~CApkIconHandler() {
        DllRelease();
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IPersistFile) {
            *ppv = static_cast<IPersistFile*>(this);
        } else if (riid == IID_IExtractIconW) {
            *ppv = static_cast<IExtractIconW*>(this);
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
        if (_szFile[0] == 0) return S_FALSE;
        
        wcsncpy_s(szIconFile, cchMax, _szFile, _TRUNCATE);
        *piIndex = 0;
        *pwFlags = GIL_PERINSTANCE | GIL_NOTFILENAME; // Cache properly, and force Extract to be called
        
        return S_OK;
    }

    IFACEMETHODIMP Extract(LPCWSTR pszFile, UINT nIconIndex, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) {
        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));

        // Open file mapping or read to memory. Reading whole zip to memory is fine for small files, but an APK can be large.
        // Let's use miniz's file initialization.
        
        // Convert wide char to utf8/mbcs
        char szPathA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, _szFile, -1, szPathA, MAX_PATH, NULL, NULL);

        if (!mz_zip_reader_init_file(&zip_archive, szPathA, 0)) {
            return S_FALSE;
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
            
            // Exclude common non-icons if we can, but let's just positively score
            int score = 0;
            if (fname.find("res/") != std::string::npos) score += 10;
            if (fname.find("mipmap") != std::string::npos) score += 10;
            
            bool is_icon = false;
            if (fname.find("launcher") != std::string::npos) { score += 1000; is_icon = true; }
            if (fname.find("ic_launcher") != std::string::npos) { score += 1000; is_icon = true; }
            if (fname.find("app_icon") != std::string::npos) { score += 800; is_icon = true; }
            if (fname.find("logo") != std::string::npos) { score += 800; is_icon = true; }
            if (fname.find("icon") != std::string::npos) { score += 500; is_icon = true; }
            
            if (fname.find("round") != std::string::npos) score += 5;

            // resolution fallback
            if (fname.find("xxxhdpi") != std::string::npos) score += 20;
            else if (fname.find("xxhdpi") != std::string::npos) score += 15;
            else if (fname.find("xhdpi") != std::string::npos) score += 10;
            else if (fname.find("hdpi") != std::string::npos) score += 5;

            // Exclude backgrounds if it doesn't have an icon name
            if (!is_icon && (fname.find("bg_") != std::string::npos || fname.find("background") != std::string::npos)) {
                score -= 100; 
            }

            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }

        if (best_idx == -1) {
            mz_zip_reader_end(&zip_archive);
            return S_FALSE;
        }

        size_t png_size = 0;
        void *png_data = mz_zip_reader_extract_to_heap(&zip_archive, best_idx, &png_size, 0);
        mz_zip_reader_end(&zip_archive);

        if (!png_data) return S_FALSE;

        IStream *pPngStream = SHCreateMemStream((const BYTE*)png_data, (UINT)png_size);
        mz_free(png_data);

        if (!pPngStream) return S_FALSE;

        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        HRESULT hr = S_FALSE;
        {
            Gdiplus::Bitmap *pBitmap = Gdiplus::Bitmap::FromStream(pPngStream);
            if (pBitmap && pBitmap->GetLastStatus() == Gdiplus::Ok) {
                int cxLarge = LOWORD(nIconSize);
                int cxSmall = HIWORD(nIconSize);
                
                if (phiconLarge) {
                    Gdiplus::Bitmap* pLarge = (Gdiplus::Bitmap*)pBitmap->GetThumbnailImage(cxLarge, cxLarge, NULL, NULL);
                    if (pLarge) {
                        pLarge->GetHICON(phiconLarge);
                        delete pLarge;
                    }
                }
                if (phiconSmall) {
                    Gdiplus::Bitmap* pSmall = (Gdiplus::Bitmap*)pBitmap->GetThumbnailImage(cxSmall, cxSmall, NULL, NULL);
                    if (pSmall) {
                        pSmall->GetHICON(phiconSmall);
                        delete pSmall;
                    }
                }
                hr = S_OK;
            }
            if (pBitmap) delete pBitmap;
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
        pPngStream->Release();

        return hr;
    }

private:
    long _cRef;
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
