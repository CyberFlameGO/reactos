/*
 *  Notepad (dialog.c)
 *
 *  Copyright 1998,99 Marcel Baur <mbaur@g26.ethz.ch>
 *  Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *  Copyright 2002 Andriy Palamarchuk
 *  Copyright 2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "notepad.h"

#include <assert.h>
#include <commctrl.h>
#include <strsafe.h>

LRESULT CALLBACK EDIT_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const TCHAR helpfile[] = _T("notepad.hlp");
static const TCHAR empty_str[] = _T("");
static const TCHAR szDefaultExt[] = _T("txt");
static const TCHAR txt_files[] = _T("*.txt");

/* Status bar parts index */
#define SBPART_CURPOS   0
#define SBPART_EOLN     1
#define SBPART_ENCODING 2

/* Line endings - string resource ID mapping table */
static UINT EolnToStrId[] = {
    STRING_CRLF,
    STRING_LF,
    STRING_CR
};

/* Encoding - string resource ID mapping table */
static UINT EncToStrId[] = {
    STRING_ANSI,
    STRING_UNICODE,
    STRING_UNICODE_BE,
    STRING_UTF8,
    STRING_UTF8_BOM
};

static UINT_PTR CALLBACK DIALOG_PAGESETUP_Hook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

VOID ShowLastError(VOID)
{
    DWORD error = GetLastError();
    if (error != NO_ERROR)
    {
        LPTSTR lpMsgBuf = NULL;
        TCHAR szTitle[MAX_STRING_LEN];

        LoadString(Globals.hInstance, STRING_ERROR, szTitle, ARRAY_SIZE(szTitle));

        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL,
                      error,
                      0,
                      (LPTSTR) &lpMsgBuf,
                      0,
                      NULL);

        MessageBox(NULL, lpMsgBuf, szTitle, MB_OK | MB_ICONERROR);
        LocalFree(lpMsgBuf);
    }
}

/**
 * Sets the caption of the main window according to Globals.szFileTitle:
 *    (untitled) - Notepad      if no file is open
 *    [filename] - Notepad      if a file is given
 */
void UpdateWindowCaption(BOOL clearModifyAlert)
{
    TCHAR szCaption[MAX_STRING_LEN];
    TCHAR szNotepad[MAX_STRING_LEN];
    TCHAR szFilename[MAX_STRING_LEN];
    BOOL isModified;

    if (clearModifyAlert)
    {
        /* When a file is being opened or created, there is no need to have
         * the edited flag shown when the file has not been edited yet. */
        isModified = FALSE;
    }
    else
    {
        /* Check whether the user has modified the file or not. If we are
         * in the same state as before, don't change the caption. */
        isModified = !!SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0);
        if (isModified == Globals.bWasModified)
            return;
    }

    /* Remember the state for later calls */
    Globals.bWasModified = isModified;

    /* Load the name of the application */
    LoadString(Globals.hInstance, STRING_NOTEPAD, szNotepad, ARRAY_SIZE(szNotepad));

    /* Determine if the file has been saved or if this is a new file */
    if (Globals.szFileTitle[0] != 0)
        StringCchCopy(szFilename, ARRAY_SIZE(szFilename), Globals.szFileTitle);
    else
        LoadString(Globals.hInstance, STRING_UNTITLED, szFilename, ARRAY_SIZE(szFilename));

    /* Update the window caption based upon whether the user has modified the file or not */
    StringCbPrintf(szCaption, sizeof(szCaption), _T("%s%s - %s"),
                   (isModified ? _T("*") : _T("")), szFilename, szNotepad);

    SetWindowText(Globals.hMainWnd, szCaption);
}

VOID DIALOG_StatusBarAlignParts(VOID)
{
    static const int defaultWidths[] = {120, 120, 120};
    RECT rcStatusBar;
    int parts[3];

    GetClientRect(Globals.hStatusBar, &rcStatusBar);

    parts[0] = rcStatusBar.right - (defaultWidths[1] + defaultWidths[2]);
    parts[1] = rcStatusBar.right - defaultWidths[2];
    parts[2] = -1; // the right edge of the status bar

    parts[0] = max(parts[0], defaultWidths[0]);
    parts[1] = max(parts[1], defaultWidths[0] + defaultWidths[1]);

    SendMessageW(Globals.hStatusBar, SB_SETPARTS, (WPARAM)ARRAY_SIZE(parts), (LPARAM)parts);
}

static VOID DIALOG_StatusBarUpdateLineEndings(VOID)
{
    WCHAR szText[128];

    LoadStringW(Globals.hInstance, EolnToStrId[Globals.iEoln], szText, ARRAY_SIZE(szText));

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_EOLN, (LPARAM)szText);
}

static VOID DIALOG_StatusBarUpdateEncoding(VOID)
{
    WCHAR szText[128] = L"";

    if (Globals.encFile != ENCODING_AUTO)
    {
        LoadStringW(Globals.hInstance, EncToStrId[Globals.encFile], szText, ARRAY_SIZE(szText));
    }

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_ENCODING, (LPARAM)szText);
}

static VOID DIALOG_StatusBarUpdateAll(VOID)
{
    DIALOG_StatusBarUpdateCaretPos();
    DIALOG_StatusBarUpdateLineEndings();
    DIALOG_StatusBarUpdateEncoding();
}

int DIALOG_StringMsgBox(HWND hParent, int formatId, LPCTSTR szString, DWORD dwFlags)
{
    TCHAR szMessage[MAX_STRING_LEN];
    TCHAR szResource[MAX_STRING_LEN];

    /* Load and format szMessage */
    LoadString(Globals.hInstance, formatId, szResource, ARRAY_SIZE(szResource));
    _sntprintf(szMessage, ARRAY_SIZE(szMessage), szResource, szString);

    /* Load szCaption */
    if ((dwFlags & MB_ICONMASK) == MB_ICONEXCLAMATION)
        LoadString(Globals.hInstance, STRING_ERROR, szResource, ARRAY_SIZE(szResource));
    else
        LoadString(Globals.hInstance, STRING_NOTEPAD, szResource, ARRAY_SIZE(szResource));

    /* Display Modal Dialog */
    // if (hParent == NULL)
        // hParent = Globals.hMainWnd;
    return MessageBox(hParent, szMessage, szResource, dwFlags);
}

static void AlertFileNotFound(LPCTSTR szFileName)
{
    DIALOG_StringMsgBox(Globals.hMainWnd, STRING_NOTFOUND, szFileName, MB_ICONEXCLAMATION | MB_OK);
}

static int AlertFileNotSaved(LPCTSTR szFileName)
{
    TCHAR szUntitled[MAX_STRING_LEN];

    LoadString(Globals.hInstance, STRING_UNTITLED, szUntitled, ARRAY_SIZE(szUntitled));

    return DIALOG_StringMsgBox(Globals.hMainWnd, STRING_NOTSAVED,
                               szFileName[0] ? szFileName : szUntitled,
                               MB_ICONQUESTION | MB_YESNOCANCEL);
}

static void AlertPrintError(void)
{
    TCHAR szUntitled[MAX_STRING_LEN];

    LoadString(Globals.hInstance, STRING_UNTITLED, szUntitled, ARRAY_SIZE(szUntitled));

    DIALOG_StringMsgBox(Globals.hMainWnd, STRING_PRINTERROR,
                        Globals.szFileName[0] ? Globals.szFileName : szUntitled,
                        MB_ICONEXCLAMATION | MB_OK);
}

/**
 * Returns:
 *   TRUE  - if file exists
 *   FALSE - if file does not exist
 */
BOOL FileExists(LPCTSTR szFilename)
{
    return GetFileAttributes(szFilename) != INVALID_FILE_ATTRIBUTES;
}

BOOL HasFileExtension(LPCTSTR szFilename)
{
    LPCTSTR s;

    s = _tcsrchr(szFilename, _T('\\'));
    if (s)
        szFilename = s;
    return _tcsrchr(szFilename, _T('.')) != NULL;
}

int GetSelectionTextLength(HWND hWnd)
{
    DWORD dwStart = 0, dwEnd = 0;
    SendMessage(hWnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
    return dwEnd - dwStart;
}

int GetSelectionText(HWND hWnd, LPTSTR lpString, int nMaxCount)
{
    DWORD dwStart = 0, dwEnd = 0;
    INT cchText = GetWindowTextLength(hWnd);
    LPTSTR pszText;
    HLOCAL hLocal;
    HRESULT hr;

    SendMessage(hWnd, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
    if (!lpString || dwStart == dwEnd || cchText == 0)
        return 0;

    hLocal = (HLOCAL)SendMessage(hWnd, EM_GETHANDLE, 0, 0);
    pszText = (LPTSTR)LocalLock(hLocal);
    if (!pszText)
        return 0;

    hr = StringCchCopyN(lpString, nMaxCount, pszText + dwStart, dwEnd - dwStart);
    LocalUnlock(hLocal);

    switch (hr)
    {
        case S_OK:
            return dwEnd - dwStart;

        case STRSAFE_E_INSUFFICIENT_BUFFER:
            return nMaxCount - 1;

        default:
            return 0;
    }
}

static RECT
GetPrintingRect(HDC hdc, RECT margins)
{
    int iLogPixelsX, iLogPixelsY;
    int iHorzRes, iVertRes;
    int iPhysPageX, iPhysPageY, iPhysPageW, iPhysPageH;
    RECT rcPrintRect;

    iPhysPageX = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    iPhysPageY = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    iPhysPageW = GetDeviceCaps(hdc, PHYSICALWIDTH);
    iPhysPageH = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    iLogPixelsX = GetDeviceCaps(hdc, LOGPIXELSX);
    iLogPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    iHorzRes = GetDeviceCaps(hdc, HORZRES);
    iVertRes = GetDeviceCaps(hdc, VERTRES);

    rcPrintRect.left = (margins.left * iLogPixelsX / 2540) - iPhysPageX;
    rcPrintRect.top = (margins.top * iLogPixelsY / 2540) - iPhysPageY;
    rcPrintRect.right = iHorzRes - (((margins.left * iLogPixelsX / 2540) - iPhysPageX) + ((margins.right * iLogPixelsX / 2540) - (iPhysPageW - iPhysPageX - iHorzRes)));
    rcPrintRect.bottom = iVertRes - (((margins.top * iLogPixelsY / 2540) - iPhysPageY) + ((margins.bottom * iLogPixelsY / 2540) - (iPhysPageH - iPhysPageY - iVertRes)));

    return rcPrintRect;
}

static BOOL DoSaveFile(VOID)
{
    BOOL bRet = FALSE;
    HANDLE hFile;
    DWORD cchText;

    hFile = CreateFileW(Globals.szFileName, GENERIC_WRITE, FILE_SHARE_WRITE,
                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ShowLastError();
        return FALSE;
    }

    cchText = GetWindowTextLengthW(Globals.hEdit);
    if (cchText <= 0)
    {
        bRet = TRUE;
    }
    else
    {
        HLOCAL hLocal = (HLOCAL)SendMessageW(Globals.hEdit, EM_GETHANDLE, 0, 0);
        LPWSTR pszText = LocalLock(hLocal);
        if (pszText)
        {
            bRet = WriteText(hFile, pszText, cchText, Globals.encFile, Globals.iEoln);
            if (!bRet)
                ShowLastError();

            LocalUnlock(hLocal);
        }
        else
        {
            ShowLastError();
        }
    }

    CloseHandle(hFile);

    if (bRet)
    {
        SendMessage(Globals.hEdit, EM_SETMODIFY, FALSE, 0);
        SetFileName(Globals.szFileName);
    }

    return bRet;
}

/**
 * Returns:
 *   TRUE  - User agreed to close (both save/don't save)
 *   FALSE - User cancelled close by selecting "Cancel"
 */
BOOL DoCloseFile(VOID)
{
    int nResult;

    if (SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0))
    {
        /* prompt user to save changes */
        nResult = AlertFileNotSaved(Globals.szFileName);
        switch (nResult)
        {
            case IDYES:
                if(!DIALOG_FileSave())
                    return FALSE;
                break;

            case IDNO:
                break;

            case IDCANCEL:
            default:
                return FALSE;
        }
    }

    SetFileName(empty_str);
    UpdateWindowCaption(TRUE);

    return TRUE;
}

VOID DoOpenFile(LPCTSTR szFileName)
{
    HANDLE hFile;
    TCHAR log[5];
    HLOCAL hLocal;

    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ShowLastError();
        goto done;
    }

    /* To make loading file quicker, we use the internal handle of EDIT control */
    hLocal = (HLOCAL)SendMessageW(Globals.hEdit, EM_GETHANDLE, 0, 0);
    if (!ReadText(hFile, &hLocal, &Globals.encFile, &Globals.iEoln))
    {
        ShowLastError();
        goto done;
    }
    SendMessageW(Globals.hEdit, EM_SETHANDLE, (WPARAM)hLocal, 0);
    /* No need of EM_SETMODIFY and EM_EMPTYUNDOBUFFER here. EM_SETHANDLE does instead. */

    SetFocus(Globals.hEdit);

    /*  If the file starts with .LOG, add a time/date at the end and set cursor after
     *  See http://web.archive.org/web/20090627165105/http://support.microsoft.com/kb/260563
     */
    if (GetWindowText(Globals.hEdit, log, ARRAY_SIZE(log)) && !_tcscmp(log, _T(".LOG")))
    {
        static const TCHAR lf[] = _T("\r\n");
        SendMessage(Globals.hEdit, EM_SETSEL, GetWindowTextLength(Globals.hEdit), -1);
        SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
        DIALOG_EditTimeDate();
        SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
    }

    SetFileName(szFileName);
    UpdateWindowCaption(TRUE);
    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();

done:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
}

VOID DIALOG_FileNew(VOID)
{
    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    SetWindowText(Globals.hEdit, NULL);
    SendMessage(Globals.hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    Globals.iEoln = EOLN_CRLF;
    Globals.encFile = ENCODING_DEFAULT;

    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();
}

VOID DIALOG_FileNewWindow(VOID)
{
    TCHAR pszNotepadExe[MAX_PATH];
    GetModuleFileName(NULL, pszNotepadExe, ARRAYSIZE(pszNotepadExe));
    ShellExecute(NULL, NULL, pszNotepadExe, NULL, NULL, SW_SHOWNORMAL);
}

VOID DIALOG_FileOpen(VOID)
{
    OPENFILENAME openfilename;
    TCHAR szPath[MAX_PATH];

    ZeroMemory(&openfilename, sizeof(openfilename));

    if (Globals.szFileName[0] == 0)
        _tcscpy(szPath, txt_files);
    else
        _tcscpy(szPath, Globals.szFileName);

    openfilename.lStructSize = sizeof(openfilename);
    openfilename.hwndOwner = Globals.hMainWnd;
    openfilename.hInstance = Globals.hInstance;
    openfilename.lpstrFilter = Globals.szFilter;
    openfilename.lpstrFile = szPath;
    openfilename.nMaxFile = ARRAY_SIZE(szPath);
    openfilename.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    openfilename.lpstrDefExt = szDefaultExt;

    if (GetOpenFileName(&openfilename)) {
        if (FileExists(openfilename.lpstrFile))
            DoOpenFile(openfilename.lpstrFile);
        else
            AlertFileNotFound(openfilename.lpstrFile);
    }
}

BOOL DIALOG_FileSave(VOID)
{
    if (Globals.szFileName[0] == 0)
    {
        return DIALOG_FileSaveAs();
    }
    else if (DoSaveFile())
    {
        UpdateWindowCaption(TRUE);
        return TRUE;
    }
    return FALSE;
}

static UINT_PTR
CALLBACK
DIALOG_FileSaveAs_Hook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TCHAR szText[128];
    HWND hCombo;

    UNREFERENCED_PARAMETER(wParam);

    switch(msg)
    {
        case WM_INITDIALOG:
            hCombo = GetDlgItem(hDlg, ID_ENCODING);

            LoadString(Globals.hInstance, STRING_ANSI, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_UNICODE, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_UNICODE_BE, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_UTF8, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_UTF8_BOM, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessage(hCombo, CB_SETCURSEL, Globals.encFile, 0);

            hCombo = GetDlgItem(hDlg, ID_EOLN);

            LoadString(Globals.hInstance, STRING_CRLF, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_LF, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, STRING_CR, szText, ARRAY_SIZE(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessage(hCombo, CB_SETCURSEL, Globals.iEoln, 0);
            break;

        case WM_NOTIFY:
            if (((NMHDR *) lParam)->code == CDN_FILEOK)
            {
                hCombo = GetDlgItem(hDlg, ID_ENCODING);
                if (hCombo)
                    Globals.encFile = (ENCODING) SendMessage(hCombo, CB_GETCURSEL, 0, 0);

                hCombo = GetDlgItem(hDlg, ID_EOLN);
                if (hCombo)
                    Globals.iEoln = (EOLN)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            }
            break;
    }
    return 0;
}

BOOL DIALOG_FileSaveAs(VOID)
{
    OPENFILENAME saveas;
    TCHAR szPath[MAX_PATH];

    ZeroMemory(&saveas, sizeof(saveas));

    if (Globals.szFileName[0] == 0)
        _tcscpy(szPath, txt_files);
    else
        _tcscpy(szPath, Globals.szFileName);

    saveas.lStructSize = sizeof(OPENFILENAME);
    saveas.hwndOwner = Globals.hMainWnd;
    saveas.hInstance = Globals.hInstance;
    saveas.lpstrFilter = Globals.szFilter;
    saveas.lpstrFile = szPath;
    saveas.nMaxFile = ARRAY_SIZE(szPath);
    saveas.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY |
                   OFN_EXPLORER | OFN_ENABLETEMPLATE | OFN_ENABLEHOOK;
    saveas.lpstrDefExt = szDefaultExt;
    saveas.lpTemplateName = MAKEINTRESOURCE(DIALOG_ENCODING);
    saveas.lpfnHook = DIALOG_FileSaveAs_Hook;

    if (GetSaveFileName(&saveas))
    {
        /* HACK: Because in ROS, Save-As boxes don't check the validity
         * of file names and thus, here, szPath can be invalid !! We only
         * see its validity when we call DoSaveFile()... */
        SetFileName(szPath);
        if (DoSaveFile())
        {
            UpdateWindowCaption(TRUE);
            DIALOG_StatusBarUpdateAll();
            return TRUE;
        }
        else
        {
            SetFileName(_T(""));
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
}

VOID DIALOG_FilePrint(VOID)
{
    DOCINFO di;
    TEXTMETRIC tm;
    PRINTDLG printer;
    SIZE szMetric;
    int border;
    int xLeft, yTop, pagecount, dopage, copycount;
    unsigned int i;
    LOGFONT hdrFont;
    HFONT font, old_font=0;
    DWORD size;
    LPTSTR pTemp;
    static const TCHAR times_new_roman[] = _T("Times New Roman");
    RECT rcPrintRect;

    /* Get a small font and print some header info on each page */
    ZeroMemory(&hdrFont, sizeof(hdrFont));
    hdrFont.lfHeight = 100;
    hdrFont.lfWeight = FW_BOLD;
    hdrFont.lfCharSet = ANSI_CHARSET;
    hdrFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    hdrFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    hdrFont.lfQuality = PROOF_QUALITY;
    hdrFont.lfPitchAndFamily = VARIABLE_PITCH | FF_ROMAN;
    _tcscpy(hdrFont.lfFaceName, times_new_roman);

    font = CreateFontIndirect(&hdrFont);

    /* Get Current Settings */
    ZeroMemory(&printer, sizeof(printer));
    printer.lStructSize = sizeof(printer);
    printer.hwndOwner = Globals.hMainWnd;
    printer.hInstance = Globals.hInstance;

    /* Set some default flags */
    printer.Flags = PD_RETURNDC | PD_SELECTION;

    /* Disable the selection radio button if there is no text selected */
    if (!GetSelectionTextLength(Globals.hEdit))
    {
        printer.Flags = printer.Flags | PD_NOSELECTION;
    }

    printer.nFromPage = 0;
    printer.nMinPage = 1;
    /* we really need to calculate number of pages to set nMaxPage and nToPage */
    printer.nToPage = (WORD)-1;
    printer.nMaxPage = (WORD)-1;

    /* Let commdlg manage copy settings */
    printer.nCopies = (WORD)PD_USEDEVMODECOPIES;

    printer.hDevMode = Globals.hDevMode;
    printer.hDevNames = Globals.hDevNames;

    if (!PrintDlg(&printer))
    {
        DeleteObject(font);
        return;
    }

    Globals.hDevMode = printer.hDevMode;
    Globals.hDevNames = printer.hDevNames;

    assert(printer.hDC != 0);

    /* initialize DOCINFO */
    di.cbSize = sizeof(DOCINFO);
    di.lpszDocName = Globals.szFileTitle;
    di.lpszOutput = NULL;
    di.lpszDatatype = NULL;
    di.fwType = 0;

    if (StartDoc(printer.hDC, &di) <= 0)
    {
        DeleteObject(font);
        return;
    }


    /* Get the file text */
    if (printer.Flags & PD_SELECTION)
    {
        size = GetSelectionTextLength(Globals.hEdit) + 1;
    }
    else
    {
        size = GetWindowTextLength(Globals.hEdit) + 1;
    }

    pTemp = HeapAlloc(GetProcessHeap(), 0, size * sizeof(TCHAR));
    if (!pTemp)
    {
        EndDoc(printer.hDC);
        DeleteObject(font);
        ShowLastError();
        return;
    }

    if (printer.Flags & PD_SELECTION)
    {
        size = GetSelectionText(Globals.hEdit, pTemp, size);
    }
    else
    {
        size = GetWindowText(Globals.hEdit, pTemp, size);
    }

    /* Get the current printing area */
    rcPrintRect = GetPrintingRect(printer.hDC, Globals.lMargins);

    /* Ensure that each logical unit maps to one pixel */
    SetMapMode(printer.hDC, MM_TEXT);

    /* Needed to get the correct height of a text line */
    GetTextMetrics(printer.hDC, &tm);

    border = 15;
    for (copycount=1; copycount <= printer.nCopies; copycount++) {
        i = 0;
        pagecount = 1;
        do {
            /* Don't start a page if none of the conditions below are true */
            dopage = 0;

            /* The user wants to print the current selection */
            if (printer.Flags & PD_SELECTION)
            {
                dopage = 1;
            }

            /* The user wants to print the entire document */
            if (!(printer.Flags & PD_PAGENUMS) && !(printer.Flags & PD_SELECTION))
            {
                dopage = 1;
            }

            /* The user wants to print a specified range of pages */
            if ((pagecount >= printer.nFromPage && pagecount <= printer.nToPage))
            {
                dopage = 1;
            }

            old_font = SelectObject(printer.hDC, font);

            if (dopage) {
                if (StartPage(printer.hDC) <= 0) {
                    SelectObject(printer.hDC, old_font);
                    EndDoc(printer.hDC);
                    DeleteDC(printer.hDC);
                    HeapFree(GetProcessHeap(), 0, pTemp);
                    DeleteObject(font);
                    AlertPrintError();
                    return;
                }

                SetViewportOrgEx(printer.hDC, rcPrintRect.left, rcPrintRect.top, NULL);

                /* Write a rectangle and header at the top of each page */
                Rectangle(printer.hDC, border, border, rcPrintRect.right - border, border + tm.tmHeight * 2);
                /* I don't know what's up with this TextOut command. This comes out
                kind of mangled.
                */
                TextOut(printer.hDC,
                        border * 2,
                        border + tm.tmHeight / 2,
                        Globals.szFileTitle,
                        lstrlen(Globals.szFileTitle));
            }

            /* The starting point for the main text */
            xLeft = 0;
            yTop = border + tm.tmHeight * 4;

            SelectObject(printer.hDC, old_font);

            /* Since outputting strings is giving me problems, output the main
             * text one character at a time. */
            do {
                if (pTemp[i] == '\n') {
                    xLeft = 0;
                    yTop += tm.tmHeight;
                }
                else if (pTemp[i] != '\r') {
                    if (dopage)
                        TextOut(printer.hDC, xLeft, yTop, &pTemp[i], 1);

                    /* We need to get the width for each individual char, since a proportional font may be used */
                    GetTextExtentPoint32(printer.hDC, &pTemp[i], 1, &szMetric);
                    xLeft += szMetric.cx;

                    /* Insert a line break if the current line does not fit into the printing area */
                    if (xLeft > rcPrintRect.right)
                    {
                        xLeft = 0;
                        yTop = yTop + tm.tmHeight;
                    }
                }
            } while (i++ < size && yTop < rcPrintRect.bottom);

            if (dopage)
                EndPage(printer.hDC);
            pagecount++;
        } while (i < size);
    }

    if (old_font != 0)
        SelectObject(printer.hDC, old_font);
    EndDoc(printer.hDC);
    DeleteDC(printer.hDC);
    HeapFree(GetProcessHeap(), 0, pTemp);
    DeleteObject(font);
}

VOID DIALOG_FileExit(VOID)
{
    PostMessage(Globals.hMainWnd, WM_CLOSE, 0, 0l);
}

VOID DIALOG_EditUndo(VOID)
{
    SendMessage(Globals.hEdit, EM_UNDO, 0, 0);
}

VOID DIALOG_EditCut(VOID)
{
    SendMessage(Globals.hEdit, WM_CUT, 0, 0);
}

VOID DIALOG_EditCopy(VOID)
{
    SendMessage(Globals.hEdit, WM_COPY, 0, 0);
}

VOID DIALOG_EditPaste(VOID)
{
    SendMessage(Globals.hEdit, WM_PASTE, 0, 0);
}

VOID DIALOG_EditDelete(VOID)
{
    SendMessage(Globals.hEdit, WM_CLEAR, 0, 0);
}

VOID DIALOG_EditSelectAll(VOID)
{
    SendMessage(Globals.hEdit, EM_SETSEL, 0, (LPARAM)-1);
}

VOID DIALOG_EditTimeDate(VOID)
{
    SYSTEMTIME st;
    TCHAR szDate[MAX_STRING_LEN];
    TCHAR szText[MAX_STRING_LEN * 2 + 2];

    GetLocalTime(&st);

    GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, szDate, MAX_STRING_LEN);
    _tcscpy(szText, szDate);
    _tcscat(szText, _T(" "));
    GetDateFormat(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, szDate, MAX_STRING_LEN);
    _tcscat(szText, szDate);
    SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)szText);
}

VOID DoShowHideStatusBar(VOID)
{
    /* Check if status bar object already exists. */
    if (Globals.bShowStatusBar && Globals.hStatusBar == NULL)
    {
        /* Try to create the status bar */
        Globals.hStatusBar = CreateStatusWindow(WS_CHILD | CCS_BOTTOM | SBARS_SIZEGRIP,
                                                NULL,
                                                Globals.hMainWnd,
                                                CMD_STATUSBAR_WND_ID);

        if (Globals.hStatusBar == NULL)
        {
            ShowLastError();
            return;
        }

        /* Load the string for formatting column/row text output */
        LoadString(Globals.hInstance, STRING_LINE_COLUMN, Globals.szStatusBarLineCol, MAX_PATH - 1);
    }

    /* Update layout of controls */
    SendMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);

    if (Globals.hStatusBar == NULL)
        return;

    /* Update visibility of status bar */
    ShowWindow(Globals.hStatusBar, (Globals.bShowStatusBar ? SW_SHOWNOACTIVATE : SW_HIDE));

    /* Update status bar contents */
    DIALOG_StatusBarUpdateAll();
}

VOID DoCreateEditWindow(VOID)
{
    DWORD dwStyle;
    int iSize;
    LPTSTR pTemp = NULL;
    BOOL bModified = FALSE;

    iSize = 0;

    /* If the edit control already exists, try to save its content */
    if (Globals.hEdit != NULL)
    {
        /* number of chars currently written into the editor. */
        iSize = GetWindowTextLength(Globals.hEdit);
        if (iSize)
        {
            /* Allocates temporary buffer. */
            pTemp = HeapAlloc(GetProcessHeap(), 0, (iSize + 1) * sizeof(TCHAR));
            if (!pTemp)
            {
                ShowLastError();
                return;
            }

            /* Recover the text into the control. */
            GetWindowText(Globals.hEdit, pTemp, iSize + 1);

            if (SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0))
                bModified = TRUE;
        }

        /* Restore original window procedure */
        SetWindowLongPtr(Globals.hEdit, GWLP_WNDPROC, (LONG_PTR)Globals.EditProc);

        /* Destroy the edit control */
        DestroyWindow(Globals.hEdit);
    }

    /* Update wrap status into the main menu and recover style flags */
    dwStyle = (Globals.bWrapLongLines ? EDIT_STYLE_WRAP : EDIT_STYLE);

    /* Create the new edit control */
    Globals.hEdit = CreateWindowEx(WS_EX_CLIENTEDGE,
                                   EDIT_CLASS,
                                   NULL,
                                   dwStyle,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   Globals.hMainWnd,
                                   NULL,
                                   Globals.hInstance,
                                   NULL);
    if (Globals.hEdit == NULL)
    {
        if (pTemp)
        {
            HeapFree(GetProcessHeap(), 0, pTemp);
        }

        ShowLastError();
        return;
    }

    SendMessage(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, FALSE);
    SendMessage(Globals.hEdit, EM_LIMITTEXT, 0, 0);

    /* If some text was previously saved, restore it. */
    if (iSize != 0)
    {
        SetWindowText(Globals.hEdit, pTemp);
        HeapFree(GetProcessHeap(), 0, pTemp);

        if (bModified)
            SendMessage(Globals.hEdit, EM_SETMODIFY, TRUE, 0);
    }

    /* Sub-class a new window callback for row/column detection. */
    Globals.EditProc = (WNDPROC)SetWindowLongPtr(Globals.hEdit,
                                                 GWLP_WNDPROC,
                                                 (LONG_PTR)EDIT_WndProc);

    /* Finally shows new edit control and set focus into it. */
    ShowWindow(Globals.hEdit, SW_SHOW);
    SetFocus(Globals.hEdit);

    /* Re-arrange controls */
    PostMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);
}

VOID DIALOG_EditWrap(VOID)
{
    Globals.bWrapLongLines = !Globals.bWrapLongLines;

    EnableMenuItem(Globals.hMenu, CMD_GOTO, (Globals.bWrapLongLines ? MF_GRAYED : MF_ENABLED));

    DoCreateEditWindow();
    DoShowHideStatusBar();
}

VOID DIALOG_SelectFont(VOID)
{
    CHOOSEFONT cf;
    LOGFONT lf = Globals.lfFont;

    ZeroMemory( &cf, sizeof(cf) );
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = Globals.hMainWnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;

    if (ChooseFont(&cf))
    {
        HFONT currfont = Globals.hFont;

        Globals.hFont = CreateFontIndirect(&lf);
        Globals.lfFont = lf;
        SendMessage(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, (LPARAM)TRUE);
        if (currfont != NULL)
            DeleteObject(currfont);
    }
}

typedef HWND (WINAPI *FINDPROC)(LPFINDREPLACE lpfr);

static VOID DIALOG_SearchDialog(FINDPROC pfnProc)
{
    if (Globals.hFindReplaceDlg != NULL)
    {
        SetFocus(Globals.hFindReplaceDlg);
        return;
    }

    ZeroMemory(&Globals.find, sizeof(Globals.find));
    Globals.find.lStructSize = sizeof(Globals.find);
    Globals.find.hwndOwner = Globals.hMainWnd;
    Globals.find.hInstance = Globals.hInstance;
    Globals.find.lpstrFindWhat = Globals.szFindText;
    Globals.find.wFindWhatLen = ARRAY_SIZE(Globals.szFindText);
    Globals.find.lpstrReplaceWith = Globals.szReplaceText;
    Globals.find.wReplaceWithLen = ARRAY_SIZE(Globals.szReplaceText);
    Globals.find.Flags = FR_DOWN;

    /* We only need to create the modal FindReplace dialog which will */
    /* notify us of incoming events using hMainWnd Window Messages    */

    Globals.hFindReplaceDlg = pfnProc(&Globals.find);
    assert(Globals.hFindReplaceDlg != NULL);
}

VOID DIALOG_Search(VOID)
{
    DIALOG_SearchDialog(FindText);
}

VOID DIALOG_SearchNext(VOID)
{
    if (Globals.find.lpstrFindWhat != NULL)
        NOTEPAD_FindNext(&Globals.find, FALSE, TRUE);
    else
        DIALOG_Search();
}

VOID DIALOG_Replace(VOID)
{
    DIALOG_SearchDialog(ReplaceText);
}

static INT_PTR
CALLBACK
DIALOG_GoTo_DialogProc(HWND hwndDialog, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    BOOL bResult = FALSE;
    HWND hTextBox;
    TCHAR szText[32];

    switch(uMsg) {
    case WM_INITDIALOG:
        hTextBox = GetDlgItem(hwndDialog, ID_LINENUMBER);
        _sntprintf(szText, ARRAY_SIZE(szText), _T("%Id"), lParam);
        SetWindowText(hTextBox, szText);
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            if (LOWORD(wParam) == IDOK)
            {
                hTextBox = GetDlgItem(hwndDialog, ID_LINENUMBER);
                GetWindowText(hTextBox, szText, ARRAY_SIZE(szText));
                EndDialog(hwndDialog, _ttoi(szText));
                bResult = TRUE;
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDialog, 0);
                bResult = TRUE;
            }
        }
        break;
    }

    return bResult;
}

VOID DIALOG_GoTo(VOID)
{
    INT_PTR nLine;
    LPTSTR pszText;
    int nLength, i;
    DWORD dwStart, dwEnd;

    nLength = GetWindowTextLength(Globals.hEdit);
    pszText = (LPTSTR) HeapAlloc(GetProcessHeap(), 0, (nLength + 1) * sizeof(*pszText));
    if (!pszText)
        return;

    /* Retrieve current text */
    GetWindowText(Globals.hEdit, pszText, nLength + 1);
    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM) &dwStart, (LPARAM) &dwEnd);

    nLine = 1;
    for (i = 0; (i < (int) dwStart) && pszText[i]; i++)
    {
        if (pszText[i] == '\n')
            nLine++;
    }

    nLine = DialogBoxParam(Globals.hInstance,
                           MAKEINTRESOURCE(DIALOG_GOTO),
                           Globals.hMainWnd,
                           DIALOG_GoTo_DialogProc,
                           nLine);

    if (nLine >= 1)
    {
        for (i = 0; pszText[i] && (nLine > 1) && (i < nLength - 1); i++)
        {
            if (pszText[i] == '\n')
                nLine--;
        }
        SendMessage(Globals.hEdit, EM_SETSEL, i, i);
        SendMessage(Globals.hEdit, EM_SCROLLCARET, 0, 0);
    }
    HeapFree(GetProcessHeap(), 0, pszText);
}

VOID DIALOG_StatusBarUpdateCaretPos(VOID)
{
    int line, col;
    TCHAR buff[MAX_PATH];
    DWORD dwStart, dwSize;

    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwSize);
    line = SendMessage(Globals.hEdit, EM_LINEFROMCHAR, (WPARAM)dwStart, 0);
    col = dwStart - SendMessage(Globals.hEdit, EM_LINEINDEX, (WPARAM)line, 0);

    _stprintf(buff, Globals.szStatusBarLineCol, line + 1, col + 1);
    SendMessage(Globals.hStatusBar, SB_SETTEXT, SBPART_CURPOS, (LPARAM)buff);
}

VOID DIALOG_ViewStatusBar(VOID)
{
    Globals.bShowStatusBar = !Globals.bShowStatusBar;
    DoShowHideStatusBar();
}

VOID DIALOG_HelpContents(VOID)
{
    WinHelp(Globals.hMainWnd, helpfile, HELP_INDEX, 0);
}

VOID DIALOG_HelpAboutNotepad(VOID)
{
    TCHAR szNotepad[MAX_STRING_LEN];
    TCHAR szNotepadAuthors[MAX_STRING_LEN];

    HICON notepadIcon = LoadIcon(Globals.hInstance, MAKEINTRESOURCE(IDI_NPICON));

    LoadString(Globals.hInstance, STRING_NOTEPAD, szNotepad, ARRAY_SIZE(szNotepad));
    LoadString(Globals.hInstance, STRING_NOTEPAD_AUTHORS, szNotepadAuthors, ARRAY_SIZE(szNotepadAuthors));

    ShellAbout(Globals.hMainWnd, szNotepad, szNotepadAuthors, notepadIcon);
    DeleteObject(notepadIcon);
}

/***********************************************************************
 *
 *           DIALOG_FilePageSetup
 */
VOID DIALOG_FilePageSetup(void)
{
    PAGESETUPDLG page;

    ZeroMemory(&page, sizeof(page));
    page.lStructSize = sizeof(page);
    page.hwndOwner = Globals.hMainWnd;
    page.Flags = PSD_ENABLEPAGESETUPTEMPLATE | PSD_ENABLEPAGESETUPHOOK | PSD_MARGINS;
    page.hInstance = Globals.hInstance;
    page.rtMargin = Globals.lMargins;
    page.hDevMode = Globals.hDevMode;
    page.hDevNames = Globals.hDevNames;
    page.lpPageSetupTemplateName = MAKEINTRESOURCE(DIALOG_PAGESETUP);
    page.lpfnPageSetupHook = DIALOG_PAGESETUP_Hook;

    PageSetupDlg(&page);

    Globals.hDevMode = page.hDevMode;
    Globals.hDevNames = page.hDevNames;
    Globals.lMargins = page.rtMargin;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *           DIALOG_PAGESETUP_Hook
 */

static UINT_PTR CALLBACK DIALOG_PAGESETUP_Hook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDOK:
                /* save user input and close dialog */
                GetDlgItemText(hDlg, 0x141, Globals.szHeader, ARRAY_SIZE(Globals.szHeader));
                GetDlgItemText(hDlg, 0x143, Globals.szFooter, ARRAY_SIZE(Globals.szFooter));
                return FALSE;

            case IDCANCEL:
                /* discard user input and close dialog */
                return FALSE;

            case IDHELP:
                {
                    /* FIXME: Bring this to work */
                    static const TCHAR sorry[] = _T("Sorry, no help available");
                    static const TCHAR help[] = _T("Help");
                    MessageBox(Globals.hMainWnd, sorry, help, MB_ICONEXCLAMATION);
                    return TRUE;
                }

            default:
                break;
            }
        }
        break;

    case WM_INITDIALOG:
        /* fetch last user input prior to display dialog */
        SetDlgItemText(hDlg, 0x141, Globals.szHeader);
        SetDlgItemText(hDlg, 0x143, Globals.szFooter);
        break;
    }

    return FALSE;
}
