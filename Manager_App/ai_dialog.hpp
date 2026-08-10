#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

struct AIProposal {
    int appIndex; // Index in dbCache["apps"]
    std::string originalName;
    std::string newName;
    std::string originalDesc;
    std::string newDesc;
    std::string originalCat;
    std::string newCat;
    std::string originalTags;
    std::string newTags;
    bool selected = true;
};

std::vector<AIProposal> g_pendingProposals;

HWND g_hAIDialog = NULL;
HWND g_hAIListView = NULL;
bool g_aiApplied = false;

LRESULT CALLBACK AIDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        g_aiApplied = false;
        SetWindowTextA(hwnd, "Verify AI Changes");

        g_hAIListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            10, 10, 760, 300, hwnd, (HMENU)100, GetModuleHandle(NULL), NULL);
        
        ListView_SetExtendedListViewStyle(g_hAIListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);

        LVCOLUMNA lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        
        const char* cols[] = { "App File", "Old Name", "New Name", "Old Cat", "New Cat", "Old Tags", "New Tags" };
        int widths[] = { 100, 100, 120, 80, 80, 100, 150 };
        for (int i = 0; i < 7; i++) {
            lvc.iSubItem = i;
            lvc.cx = widths[i];
            lvc.pszText = (LPSTR)cols[i];
            ListView_InsertColumn(g_hAIListView, i, &lvc);
        }

        for (size_t i = 0; i < g_pendingProposals.size(); i++) {
            LVITEMA lvi = { 0 };
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.lParam = i;
            // Get original filename? We can just put original name
            lvi.pszText = (LPSTR)g_pendingProposals[i].originalName.c_str();
            ListView_InsertItem(g_hAIListView, &lvi);
            ListView_SetItemText(g_hAIListView, i, 1, (LPSTR)g_pendingProposals[i].originalName.c_str());
            ListView_SetItemText(g_hAIListView, i, 2, (LPSTR)g_pendingProposals[i].newName.c_str());
            ListView_SetItemText(g_hAIListView, i, 3, (LPSTR)g_pendingProposals[i].originalCat.c_str());
            ListView_SetItemText(g_hAIListView, i, 4, (LPSTR)g_pendingProposals[i].newCat.c_str());
            ListView_SetItemText(g_hAIListView, i, 5, (LPSTR)g_pendingProposals[i].originalTags.c_str());
            ListView_SetItemText(g_hAIListView, i, 6, (LPSTR)g_pendingProposals[i].newTags.c_str());
            
            ListView_SetCheckState(g_hAIListView, i, TRUE);
        }

        CreateWindowA("BUTTON", "Apply Selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 320, 120, 30, hwnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 400, 320, 100, 30, hwnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
        
        // Center window
        RECT parentRc, rc;
        GetWindowRect(GetParent(hwnd), &parentRc);
        GetWindowRect(hwnd, &rc);
        int x = parentRc.left + (parentRc.right - parentRc.left - 800) / 2;
        int y = parentRc.top + (parentRc.bottom - parentRc.top - 400) / 2;
        SetWindowPos(hwnd, NULL, x, y, 800, 400, SWP_NOZORDER);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {
            for (size_t i = 0; i < g_pendingProposals.size(); i++) {
                g_pendingProposals[i].selected = ListView_GetCheckState(g_hAIListView, i);
            }
            g_aiApplied = true;
            EndDialog(hwnd, IDOK);
        } else if (id == IDCANCEL) {
            EndDialog(hwnd, IDCANCEL);
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hwnd, IDCANCEL);
        break;
    }
    return FALSE;
}

bool ShowAIVerifyDialog(HWND parent) {
    // Create a simple dialog box in memory or just use a window.
    // For simplicity, we can create a modeless window and run a local message loop, or create a dialog from memory template.
    // Here we'll build a memory template.
    
    #pragma pack(push, 1)
    struct DLGTEMPLATEEX {
        WORD dlgVer;
        WORD signature;
        DWORD helpID;
        DWORD exStyle;
        DWORD style;
        WORD cDlgItems;
        short x; short y; short cx; short cy;
        WORD menu;
        WORD windowClass;
        WCHAR title[1];
        WORD pointsize;
        WORD weight;
        BYTE italic;
        BYTE charset;
        WCHAR typeface[1];
    };
    #pragma pack(pop)

    HGLOBAL hglb = GlobalAlloc(GMEM_ZEROINIT, 1024);
    DLGTEMPLATEEX* pDlg = (DLGTEMPLATEEX*)GlobalLock(hglb);
    pDlg->dlgVer = 1;
    pDlg->signature = 0xFFFF;
    pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_CENTER;
    pDlg->cDlgItems = 0;
    pDlg->cx = 400; pDlg->cy = 200; // Base dialog units
    GlobalUnlock(hglb);

    INT_PTR res = DialogBoxIndirectParamA(GetModuleHandle(NULL), (LPCDLGTEMPLATE)hglb, parent, (DLGPROC)AIDialogProc, 0);
    GlobalFree(hglb);
    return g_aiApplied;
}
