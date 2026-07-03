/* ---------------------------------------------------------
 *  _____      _             _____          _
 * |  __ \    | |           |  __ \        | |
 * | |__) |___| |_ _ __ ___ | |__) |_ _  __| |
 * |  _  // _ \ __| '__/ _ \|  ___/ _` |/ _` |
 * | | \ \  __/ |_| | | (_) | |  | (_| | (_| |
 * |_|  \_\___|\__|_|  \___/|_|   \__,_|\__,_|
 * T I N Y  c   D E S K T O P   E D I T O R
 * ---------------------------------------------------------
 * C translation of Dave's Tiny Editor (DTE) / TinyRetroPad
 * Original MASM source (c) 2026 Plummer's Software, Ltd.
 * (c) 2026 Matthew M. Power - Apache License 2.0
 * ---------------------------------------------------------
 *
 * Build (MSVC):
 *   cl /O1 tinyretropad.c user32.lib kernel32.lib gdi32.lib ^
 *      comdlg32.lib shell32.lib /link /subsystem:windows
 *
 * Build (MinGW):
 *   gcc -O2 -mwindows -o tinyretropad.exe tinyretropad.c ^
 *      -lcomdlg32 -lgdi32 -lshell32
 */

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>

/* =====================  FEATURE MENU  =====================
 * Optional features are gated behind compile-time switches.
 * Set a switch to 1 to compile the feature in, or 0 to leave
 * it out entirely.  With every switch 0 the output matches
 * the original baseline build; a feature only costs space
 * when it is switched on.
 * ========================================================== */
#define FEAT_LINENUMBERS 0      /* View > Line Numbers gutter (default OFF) */
#define FEAT_DARKMODE    0      /* View > Dark Mode           (default OFF) */

#define WindowWidth      800    /* window startup size */
#define WindowHeight     640
#define MAX_CMD_PATH     128    /* holds startup file path from dropped file */
#define MAX_TITLE        128    /* holds window title text (file name and if dirty *) */
#define SBHEIGHT         20     /* status bar height in pixels */
#define IDC_GOEDIT       1000   /* Go To dialog edit field id */

/* menu command ids (WM_SYSCOMMAND ids must have low 4 bits clear) */
#define IDM_SAVE            0xE100  /* Save menu ID (also on system menu) */
#define IDM_FILE_NEW        0xE200
#define IDM_FILE_EXIT       0xE201
#define IDM_FILE_OPEN       0xE202
#define IDM_FILE_SAVEAS     0xE203
#define IDM_FILE_PRINT      0xE204
#define IDM_FILE_PAGESETUP  0xE205
#define IDM_EDIT_UNDO       0xE210
#define IDM_EDIT_CUT        0xE211
#define IDM_EDIT_COPY       0xE212
#define IDM_EDIT_PASTE      0xE213
#define IDM_EDIT_DELETE     0xE214
#define IDM_EDIT_SELALL     0xE215
#define IDM_EDIT_TIME       0xE216
#define IDM_EDIT_FIND       0xE217
#define IDM_EDIT_FINDNEXT   0xE218
#define IDM_EDIT_REPLACE    0xE219
#define IDM_EDIT_GOTO       0xE21A
#define IDM_FMT_WRAP        0xE220
#define IDM_FMT_FONT        0xE221
#define IDM_VIEW_STATUS     0xE230
#define IDM_HELP_ABOUT      0xE240
#define IDM_HELP_VIEWHELP   0xE241

#if FEAT_LINENUMBERS
#define LN_MARGIN_W         44      /* gutter width in pixels */
#define LN_PAD              6       /* left padding of the numbers */
#define IDM_VIEW_LINENUM    0xE231  /* View > Line Numbers command id */
#endif

#if FEAT_DARKMODE
#define DARK_BG   RGB(0x1E,0x1E,0x1E)   /* gutter/edit dark background */
#define DARK_FG   RGB(0xDC,0xDC,0xDC)   /* light text on dark */
#define IDM_VIEW_DARK       0xE232      /* View > Dark Mode command id */
#endif

/* ------------------------- globals ------------------------- */

static const char ClassName[] = ".";            /* save bytes here (seems to work) */
static const char RichDll[]   = "Msftedit";     /* Rich Edit DLL (no ext saves those bytes) */
static const char EditClass[] = "RICHEDIT50W";  /* modern Rich Edit control from WinAPI */
static const char SaveText[]  = "Save";         /* button added to system menu */
static const char EmptyText[] = "";

static HWND  hMain;                     /* main window handle */
static HWND  hEdit;                     /* EDIT control handle */
static char  CmdFile[MAX_CMD_PATH];     /* startup file path buffer */
static char  TitleBuf[MAX_TITLE];       /* window title buffer */
static DWORD BytesRead;                 /* bytes read from file */
static int   fDirty;                    /* EDIT modified flag */
static int   fWrap = 1;                 /* word wrap state */

static const char UntitledText[] = "Untitled";
static const char NotepadTail[]  = " - TinyRetroPad";

static const char AboutCap[]    = "TinyRetroPad";
static const char AboutText[]   = "TinyRetroPad - tiny notepad-style editor";
static const char SaveCap[]     = "TinyRetroPad";
static const char SaveAskText[] = "Save changes?";
static const char SpaceText[]   = " ";
static char  DateBuf[32];
static char  TimeBuf[32];
static const char FileFilter[]  = "All Files\0*.*\0";

static char         FindWhat[128];      /* Find What text buffer */
static char         ReplaceWith[128];   /* Replace With text buffer */
static FINDREPLACEA fr;                 /* shared find/replace request */
static HWND         hFindDlg;           /* modeless find/replace dialog HWND */
static UINT         uFindMsg;           /* registered FINDMSGSTRING message */

static const char DocName[]  = "TinyRetroPad";  /* print job document name */
static const char LnColFmt[] = "  Ln %d, Col %d"; /* status bar Ln/Col format */
static char  StatusBuf[48];             /* formatted Ln/Col text */
static HWND  hStatus;                   /* status bar window handle */
static int   fStatus = 1;               /* status bar visible flag (default ON) */

#if FEAT_LINENUMBERS
static int   fLineNum;                  /* line-number gutter visible flag (default OFF) */
#endif
#if FEAT_DARKMODE
static int   fDark;                     /* dark mode flag (default OFF) */
#endif

static HINSTANCE hInst;                 /* module handle (for dialogs) */
static const char HelpUrl[] = "https://github.com/davepl";

/* Rich Edit default font face: Courier only */
static CHARFORMATW RichFont = {
    sizeof(CHARFORMATW),                /* cbSize   */
    CFM_FACE,                           /* dwMask: only set face name */
    0, 0, 0, 0,                         /* no effects/size/offset/color */
    0, 0,                               /* default charset, pitch/family */
    L"Courier"
};

/* forward */
static void SaveFile(void);
static void LoadStartupFile(void);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

/* ---------------------------------------------------------
 * title bar caption from startup file name
 * add "*" if the buffer has been modified (per original,
 * the star is reserved; only the base name + tail is set)
 * --------------------------------------------------------- */
static void BuildTitle(void)
{
    char       *dst = TitleBuf;
    const char *src = CmdFile;

    if (*src == 0) {
        /* no file argument: use Untitled */
        src = UntitledText;
    } else {
        /* strip full path to filename when a file was provided */
        const char *tail = src;
        const char *p;
        for (p = src; *p; ++p) {
            if (*p == '\\')             /* '/' and ':' checks were disabled in the original */
                tail = p + 1;
        }
        src = tail;                     /* point to filename (tail of path) */
    }

    /* copy the filename into the title buffer */
    while (*src)
        *dst++ = *src++;

    /* append " - TinyRetroPad" (including terminator) */
    src = NotepadTail;
    do {
        *dst++ = *src;
    } while (*src++);
}

/* build title and set title bar caption */
static void ApplyTitle(void)
{
    BuildTitle();
    SetWindowTextA(hMain, TitleBuf);
}

/* ---------------------------------------------------------
 * parse command line for the startup file or
 * if user drops a file on the app to launch
 * --------------------------------------------------------- */
static void ParseStartupFile(void)
{
    const char *s = GetCommandLineA();
    char *dst;
    int   n;

    CmdFile[0] = 0;
    if (!s)
        return;

    if (*s == '"') {
        /* skip quoted exe path so s points to first argument */
        ++s;
        while (*s && *s != '"')
            ++s;
        if (*s == '"')
            ++s;
    } else {
        /* skip unquoted exe path to reach first argument */
        while (*s && *s != ' ' && *s != '\t')
            ++s;
    }

    /* skip spaces/tabs before argument (white spaces) */
    while (*s == ' ' || *s == '\t')
        ++s;

    if (*s == 0)
        return;                         /* no arg: no startup file */

    /* start copying first argument (file path), handle quoted */
    dst = CmdFile;
    n   = MAX_CMD_PATH - 1;

    if (*s == '"') {
        /* copy quoted file path into CmdFile (strip quotes) */
        ++s;
        while (*s && *s != '"' && n--)
            *dst++ = *s++;
    } else {
        /* copy unquoted file path into CmdFile */
        while (*s && *s != ' ' && *s != '\t' && n--)
            *dst++ = *s++;
    }
    *dst = 0;
}

/* start a new empty document */
static void NewFile(void)
{
    CmdFile[0] = 0;
    SetWindowTextA(hEdit, EmptyText);
    fDirty = 0;
    ApplyTitle();
}

/* open-file dialog into CmdFile buffer */
static BOOL PickOpenFile(void)
{
    OPENFILENAMEA ofn = {0};

    CmdFile[0]       = 0;
    ofn.lStructSize  = sizeof(OPENFILENAMEA);
    ofn.hwndOwner    = hMain;
    ofn.lpstrFilter  = FileFilter;
    ofn.lpstrFile    = CmdFile;
    ofn.nMaxFile     = MAX_CMD_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    return GetOpenFileNameA(&ofn);
}

/* save-file dialog into CmdFile buffer */
static BOOL PickSaveFile(void)
{
    OPENFILENAMEA ofn = {0};

    ofn.lStructSize  = sizeof(OPENFILENAMEA);
    ofn.hwndOwner    = hMain;
    ofn.lpstrFilter  = FileFilter;
    ofn.lpstrFile    = CmdFile;
    ofn.nMaxFile     = MAX_CMD_PATH;
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;

    return GetSaveFileNameA(&ofn);
}

/* ---------------------------------------------------------
 * ask user whether to save dirty buffer
 * returns 1 continue / 0 cancel
 * --------------------------------------------------------- */
static int MaybeSaveChanges(void)
{
    int r;

    if (!fDirty)
        return 1;

    r = MessageBoxA(hMain, SaveAskText, SaveCap,
                    MB_YESNOCANCEL | MB_ICONQUESTION);

    if (r == IDCANCEL)
        return 0;
    if (r == IDNO)
        return 1;

    /* need save: prompt for a name first if we have none */
    if (CmdFile[0] == 0) {
        if (!PickSaveFile())
            return 0;
    }
    SaveFile();
    return 1;
}

/* insert current date/time at caret */
static void InsertTimeDate(void)
{
    SYSTEMTIME sysTime;

    GetLocalTime(&sysTime);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &sysTime, NULL, DateBuf, 32);
    GetTimeFormatA(LOCALE_USER_DEFAULT, 0,              &sysTime, NULL, TimeBuf, 32);

    SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)DateBuf);
    SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)SpaceText);
    SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)TimeBuf);
}

/* toggle Rich Edit word-wrap mode */
static void ToggleWrap(void)
{
    if (fWrap) {
        /* wrap off: use a very wide target line */
        fWrap = 0;
        SendMessageA(hEdit, EM_SETTARGETDEVICE, 0, (LPARAM)0xFFFFFFFF);
    } else {
        fWrap = 1;
        SendMessageA(hEdit, EM_SETTARGETDEVICE, 0, 0);
    }
}

/* pick a font via common dialog, apply to text */
static void ChooseFontDlg(void)
{
    LOGFONTW    lf  = {0};
    CHOOSEFONTW cf  = {0};
    CHARFORMATW fmt = {0};
    int i;

    cf.lStructSize = sizeof(CHOOSEFONTW);
    cf.hwndOwner   = hMain;
    cf.lpLogFont   = &lf;
    cf.Flags       = CF_SCREENFONTS | CF_EFFECTS;

    if (!ChooseFontW(&cf))
        return;

    /* build CHARFORMATW from chosen font */
    fmt.cbSize = sizeof(CHARFORMATW);
    fmt.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC;

    /* bold/italic effects */
    if (lf.lfWeight >= 700)
        fmt.dwEffects |= CFE_BOLD;
    if (lf.lfItalic)
        fmt.dwEffects |= CFE_ITALIC;

    /* yHeight (twips) = iPointSize (1/10 pt) * 2 */
    fmt.yHeight = cf.iPointSize * 2;

    /* copy wide face name into CHARFORMATW */
    for (i = 0; i < LF_FACESIZE; ++i) {
        fmt.szFaceName[i] = lf.lfFaceName[i];
        if (!lf.lfFaceName[i])
            break;
    }

    SendMessageA(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&fmt);
}

/* fill shared FINDREPLACE request struct */
static void InitFR(void)
{
    ZeroMemory(&fr, sizeof(fr));
    fr.lStructSize      = sizeof(FINDREPLACEA);
    fr.hwndOwner        = hMain;
    fr.lpstrFindWhat    = FindWhat;
    fr.wFindWhatLen     = 128;
    fr.lpstrReplaceWith = ReplaceWith;
    fr.wReplaceWithLen  = 128;
    fr.Flags            = FR_DOWN;
}

/* ---------------------------------------------------------
 * find next match of FindWhat, select it
 * returns 1 found / 0 not found
 * --------------------------------------------------------- */
static int DoFindNext(void)
{
    FINDTEXTEXA ft;
    CHARRANGE   cr;
    LRESULT     pos;
    WPARAM      flags;

    /* current selection range */
    SendMessageA(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    /* search from end of selection to end of text */
    ft.chrg.cpMin = cr.cpMax;
    ft.chrg.cpMax = -1;
    ft.lpstrText  = FindWhat;

    flags = (fr.Flags & FR_MATCHCASE) | FR_DOWN;

    pos = SendMessageA(hEdit, EM_FINDTEXTEX, flags, (LPARAM)&ft);
    if (pos == -1)
        return 0;

    /* select the match */
    SendMessageA(hEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
    SendMessageA(hEdit, EM_SCROLLCARET, 0, 0);
    return 1;
}

/* replace current match then advance */
static void DoReplaceOne(void)
{
    SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)ReplaceWith);
    DoFindNext();
}

/* replace every match from the top */
static void DoReplaceAll(void)
{
    CHARRANGE cr;

    /* move caret to start of text */
    cr.cpMin = 0;
    cr.cpMax = 0;
    SendMessageA(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    while (DoFindNext())
        SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)ReplaceWith);
}

/* dispatch a FINDMSGSTRING notification */
static void OnFindReplaceMsg(void)
{
    DWORD f = fr.Flags;

    if (f & FR_DIALOGTERM) {
        hFindDlg = NULL;
        return;
    }
    if (f & FR_REPLACEALL) {
        DoReplaceAll();
        return;
    }
    if (f & FR_REPLACE) {
        DoReplaceOne();
        return;
    }
    DoFindNext();
}

/* print document via common dialog + Rich Edit */
static void PrintDoc(void)
{
    PRINTDLGA   pd     = {0};
    DOCINFOA    docInf = {0};
    FORMATRANGE fmt    = {0};
    LONG        txtLen;
    HDC         hPrnDC;
    int         w, h;

    /* show Print dialog, request a printer DC */
    pd.lStructSize = sizeof(PRINTDLGA);
    pd.hwndOwner   = hMain;
    pd.Flags       = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    if (!PrintDlgA(&pd))
        return;
    hPrnDC = pd.hDC;

    /* begin document */
    docInf.cbSize      = sizeof(DOCINFOA);
    docInf.lpszDocName = DocName;
    StartDocA(hPrnDC, &docInf);

    /* prepare FORMATRANGE */
    fmt.hdc       = hPrnDC;
    fmt.hdcTarget = hPrnDC;

    /* page width in twips = HORZRES * 1440 / LOGPIXELSX */
    w = GetDeviceCaps(hPrnDC, HORZRES);
    fmt.rc.right  = fmt.rcPage.right  = MulDiv(w, 1440, GetDeviceCaps(hPrnDC, LOGPIXELSX));

    /* page height in twips = VERTRES * 1440 / LOGPIXELSY */
    h = GetDeviceCaps(hPrnDC, VERTRES);
    fmt.rc.bottom = fmt.rcPage.bottom = MulDiv(h, 1440, GetDeviceCaps(hPrnDC, LOGPIXELSY));

    /* render range = whole document */
    txtLen = (LONG)SendMessageA(hEdit, WM_GETTEXTLENGTH, 0, 0);
    fmt.chrg.cpMin = 0;
    fmt.chrg.cpMax = txtLen;

    do {
        StartPage(hPrnDC);
        fmt.chrg.cpMin = (LONG)SendMessageA(hEdit, EM_FORMATRANGE, TRUE, (LPARAM)&fmt);
        EndPage(hPrnDC);                /* next page starts at returned index */
    } while (fmt.chrg.cpMin < txtLen);

    /* flush formatting cache, end document, free DC */
    SendMessageA(hEdit, EM_FORMATRANGE, 0, 0);
    EndDoc(hPrnDC);
    DeleteDC(hPrnDC);
}

/* refresh status bar Ln/Col from caret pos */
static void UpdateStatus(void)
{
    CHARRANGE cr;
    LRESULT   line, lineStart;

    if (!fStatus)
        return;

    /* current caret character index */
    SendMessageA(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    /* 0-based line of caret -> 1-based */
    line = SendMessageA(hEdit, EM_EXLINEFROMCHAR, 0, cr.cpMax);

    /* first char index of that line */
    lineStart = SendMessageA(hEdit, EM_LINEINDEX, line, 0);

    /* column = caret - lineStart + 1; format and display */
    wsprintfA(StatusBuf, LnColFmt, (int)(line + 1), (int)(cr.cpMax - lineStart + 1));
    SetWindowTextA(hStatus, StatusBuf);
}

/* re-lay-out edit/status using client size */
static void RelayoutClient(void)
{
    RECT rc;
    GetClientRect(hMain, &rc);
    SendMessageA(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
}

#if FEAT_LINENUMBERS
/* invalidate the line-number gutter strip */
static void LnInvalidate(HWND hW)
{
    RECT rc;
    if (!fLineNum)
        return;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = LN_MARGIN_W;
    rc.bottom = 0x7FFF;
    InvalidateRect(hW, &rc, FALSE);
}
#endif

/* show the common Page Setup dialog */
static void PageSetup(void)
{
    PAGESETUPDLGA psd = {0};
    psd.lStructSize = sizeof(PAGESETUPDLGA);
    psd.hwndOwner   = hMain;
    PageSetupDlgA(&psd);
}

/* Go To dialog procedure */
static INT_PTR CALLBACK GoToProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    if (uMsg == WM_INITDIALOG)
        return TRUE;

    if (uMsg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDOK:
            EndDialog(hDlg, GetDlgItemInt(hDlg, IDC_GOEDIT, NULL, FALSE));
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, 0);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------------------------------------------------
 * in-memory Go To dialog template (no font block to stay
 * compact) - built as raw words exactly like the original
 * --------------------------------------------------------- */
#pragma pack(push, 4)
static const struct {
    /* DLGTEMPLATE */
    DWORD style;
    DWORD dwExtendedStyle;
    WORD  cdit;
    WORD  x, y, cx, cy;
    WORD  menu;                 /* no menu */
    WORD  windowClass;          /* default class */
    WCHAR caption[6];           /* "Go To" */
    WORD  pad0;                 /* align next item on DWORD */

    /* DLGITEMTEMPLATE: edit field */
    DWORD style1;
    DWORD exStyle1;
    WORD  x1, y1, cx1, cy1;
    WORD  id1;
    WORD  cls1[2];              /* 0xFFFF, 0x0081 = Edit class atom */
    WORD  title1;               /* no caption */
    WORD  cdata1;               /* no creation data */

    /* DLGITEMTEMPLATE: OK button */
    DWORD style2;
    DWORD exStyle2;
    WORD  x2, y2, cx2, cy2;
    WORD  id2;
    WORD  cls2[2];              /* 0xFFFF, 0x0080 = Button class atom */
    WCHAR title2[3];            /* "OK" */
    WORD  cdata2;               /* no creation data */
} GoToTmpl = {
    DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU,
    0,
    2,                          /* control count */
    0, 0, 150, 46,              /* x,y,cx,cy */
    0, 0,
    L"Go To",
    0,

    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | WS_TABSTOP,
    0,
    7, 7, 136, 12,              /* edit rect */
    IDC_GOEDIT,
    { 0xFFFF, 0x0081 },
    0, 0,

    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
    0,
    50, 26, 50, 14,             /* OK button rect */
    IDOK,
    { 0xFFFF, 0x0080 },
    L"OK",
    0
};
#pragma pack(pop)

/* prompt for a line number and jump to it */
static void GoToDlg(void)
{
    CHARRANGE cr;
    INT_PTR   line;
    LRESULT   idx;

    line = DialogBoxIndirectParamA(hInst, (LPCDLGTEMPLATE)&GoToTmpl,
                                   hMain, GoToProc, 0);

    /* result = 1-based line, 0 = cancel/invalid */
    if (line == 0)
        return;
    --line;

    /* char index of that line's first character */
    idx = SendMessageA(hEdit, EM_LINEINDEX, (WPARAM)line, 0);
    if (idx == -1)
        return;

    /* move caret there and scroll into view */
    cr.cpMin = (LONG)idx;
    cr.cpMax = (LONG)idx;
    SendMessageA(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageA(hEdit, EM_SCROLLCARET, 0, 0);

    /* activate the edit control so the caret shows */
    SetFocus(hEdit);
}

/* append one enabled menu item */
static void AppendEnabled(HMENU hMenu, UINT uID, const char *pText)
{
    AppendMenuA(hMenu, MF_STRING, uID, pText);
}

/* append one disabled menu item / separator */
static void AppendDisabled(HMENU hMenu, const char *pText)
{
    if (pText)
        AppendMenuA(hMenu, MF_STRING | MF_GRAYED, 0, pText);
    else
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
}

/* right-click context menu at cursor position */
static void ShowContextMenu(HWND hWndOwner)
{
    HMENU hCtx = CreatePopupMenu();
    DWORD pos;

    AppendEnabled (hCtx, IDM_EDIT_UNDO,   "&Undo");
    AppendDisabled(hCtx, NULL);
    AppendEnabled (hCtx, IDM_EDIT_CUT,    "Cu&t");
    AppendEnabled (hCtx, IDM_EDIT_COPY,   "&Copy");
    AppendEnabled (hCtx, IDM_EDIT_PASTE,  "&Paste");
    AppendEnabled (hCtx, IDM_EDIT_DELETE, "De&lete");
    AppendDisabled(hCtx, NULL);
    AppendEnabled (hCtx, IDM_EDIT_SELALL, "Select &All");

    pos = GetMessagePos();
    TrackPopupMenu(hCtx, 0, GET_X_LPARAM(pos), GET_Y_LPARAM(pos), 0, hWndOwner, NULL);
    DestroyMenu(hCtx);
}

/* build full top menu bar from menu tables */
static void CreateNotepadMenus(HWND hWnd)
{
    HMENU hMenuBar = CreateMenu();
    HMENU hPopup;

    if (!hMenuBar)
        return;

    /* File */
    hPopup = CreatePopupMenu();
    AppendEnabled (hPopup, IDM_FILE_NEW,       "&New");
    AppendEnabled (hPopup, IDM_FILE_OPEN,      "&Open...");
    AppendEnabled (hPopup, IDM_SAVE,           "&Save");
    AppendEnabled (hPopup, IDM_FILE_SAVEAS,    "Save &As...");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_FILE_PAGESETUP, "Page Set&up...");
    AppendEnabled (hPopup, IDM_FILE_PRINT,     "&Print...");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_FILE_EXIT,      "E&xit");
    AppendMenuA(hMenuBar, MF_POPUP | MF_STRING, (UINT_PTR)hPopup, "&File");

    /* Edit */
    hPopup = CreatePopupMenu();
    AppendEnabled (hPopup, IDM_EDIT_UNDO,     "&Undo");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_EDIT_CUT,      "Cu&t");
    AppendEnabled (hPopup, IDM_EDIT_COPY,     "&Copy");
    AppendEnabled (hPopup, IDM_EDIT_PASTE,    "&Paste");
    AppendEnabled (hPopup, IDM_EDIT_DELETE,   "De&lete");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_EDIT_FIND,     "&Find...");
    AppendEnabled (hPopup, IDM_EDIT_FINDNEXT, "Find &Next");
    AppendEnabled (hPopup, IDM_EDIT_REPLACE,  "&Replace...");
    AppendEnabled (hPopup, IDM_EDIT_GOTO,     "&Go To...");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_EDIT_SELALL,   "Select &All");
    AppendEnabled (hPopup, IDM_EDIT_TIME,     "Time/&Date");
    AppendMenuA(hMenuBar, MF_POPUP | MF_STRING, (UINT_PTR)hPopup, "&Edit");

    /* Format */
    hPopup = CreatePopupMenu();
    AppendEnabled(hPopup, IDM_FMT_WRAP, "&Word Wrap");
    AppendEnabled(hPopup, IDM_FMT_FONT, "&Font...");
    AppendMenuA(hMenuBar, MF_POPUP | MF_STRING, (UINT_PTR)hPopup, "F&ormat");

    /* View */
    hPopup = CreatePopupMenu();
    AppendEnabled(hPopup, IDM_VIEW_STATUS, "&Status Bar");
#if FEAT_LINENUMBERS
    AppendEnabled(hPopup, IDM_VIEW_LINENUM, "Line &Numbers");
#endif
#if FEAT_DARKMODE
    AppendEnabled(hPopup, IDM_VIEW_DARK, "Dark &Mode");
#endif
    AppendMenuA(hMenuBar, MF_POPUP | MF_STRING, (UINT_PTR)hPopup, "&View");

    /* Help */
    hPopup = CreatePopupMenu();
    AppendEnabled (hPopup, IDM_HELP_VIEWHELP, "&View Help");
    AppendDisabled(hPopup, NULL);
    AppendEnabled (hPopup, IDM_HELP_ABOUT,    "&About TinyRetroPad");
    AppendMenuA(hMenuBar, MF_POPUP | MF_STRING, (UINT_PTR)hPopup, "&Help");

    SetMenu(hWnd, hMenuBar);
}

/* read startup file and populate EDIT control */
static void LoadStartupFile(void)
{
    HANDLE hFile;
    char  *hMem;
    DWORD  dwSize;

    /* open CmdFile for reading */
    hFile = CreateFileA(CmdFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    /* get file size (bytes) */
    dwSize = GetFileSize(hFile, NULL);
    if (dwSize == 0xFFFFFFFF)
        goto CloseOnly;

    /* alloc buffer for file (+1 for null) */
    hMem = (char *)GlobalAlloc(GMEM_FIXED, dwSize + 1);
    if (!hMem)
        goto CloseOnly;

    /* read file into buffer */
    if (ReadFile(hFile, hMem, dwSize, &BytesRead, NULL)) {
        /* null-terminate loaded file data */
        hMem[BytesRead] = 0;

        /* set EDIT text from buffer */
        SetWindowTextA(hEdit, hMem);

        /* set Rich Edit font on loaded text */
        SendMessageA(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&RichFont);
    }

    /* free loaded file buffer */
    GlobalFree(hMem);

CloseOnly:
    CloseHandle(hFile);
}

/* save EDIT contents back to CmdFile */
static void SaveFile(void)
{
    HANDLE hFile;
    char  *hMem;
    DWORD  dwSize;

    /* get EDIT text length and alloc buffer (+1) */
    dwSize = (DWORD)SendMessageA(hEdit, WM_GETTEXTLENGTH, 0, 0);
    hMem   = (char *)GlobalAlloc(GMEM_FIXED, dwSize + 1);
    if (!hMem)
        return;

    /* get EDIT text into buffer */
    SendMessageA(hEdit, WM_GETTEXT, dwSize + 1, (LPARAM)hMem);

    /* open CmdFile for write (overwrite) */
    hFile = CreateFileA(CmdFile, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        /* write buffer to file, close */
        WriteFile(hFile, hMem, dwSize, &BytesRead, NULL);
        CloseHandle(hFile);

        /* clear dirty flag and update title */
        fDirty = 0;
        ApplyTitle();
    }

    /* cleanup - free save buffer */
    GlobalFree(hMem);
}

/* ---------------------------------------------------------
 * program entry point
 * --------------------------------------------------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WNDCLASSA wc = {0};
    MSG       msg;

    (void)hPrev; (void)lpCmd; (void)nShow;

    hInst = hInstance ? hInstance : GetModuleHandleA(NULL);

    /* load modern Rich Edit control library */
    LoadLibraryA(RichDll);

    /* register the common Find/Replace notification message */
    uFindMsg = RegisterWindowMessageA(FINDMSGSTRINGA);

    /* register window class */
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = ClassName;
    RegisterClassA(&wc);

    /* parse command line for startup file */
    ParseStartupFile();

    /* create main application window */
    hMain = CreateWindowExA(0, ClassName, ClassName,
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            WindowWidth, WindowHeight,
                            NULL, NULL, hInst, NULL);
    if (!hMain)
        ExitProcess(0);

    /* load file and set title */
    LoadStartupFile();
    fDirty = 0;
    ApplyTitle();

    while (GetMessageA(&msg, NULL, 0, 0)) {

        /* let an active find/replace dialog handle its own keys */
        if (hFindDlg && IsDialogMessageA(hFindDlg, &msg))
            continue;

        /* manual Notepad-style accelerators not supplied by the EDIT control */
        if (msg.message == WM_KEYDOWN) {
            UINT cmd = 0;

            if (msg.wParam == VK_F3)
                cmd = IDM_EDIT_FINDNEXT;
            else if (msg.wParam == VK_F5)
                cmd = IDM_EDIT_TIME;
            else if (GetKeyState(VK_CONTROL) & 0x8000) {
                switch (msg.wParam) {
                case 'N': cmd = IDM_FILE_NEW;     break;
                case 'O': cmd = IDM_FILE_OPEN;    break;
                case 'S': cmd = (GetKeyState(VK_SHIFT) & 0x8000)
                                ? IDM_FILE_SAVEAS : IDM_SAVE;
                          break;
                case 'P': cmd = IDM_FILE_PRINT;   break;
                case 'F': cmd = IDM_EDIT_FIND;    break;
                case 'H': cmd = IDM_EDIT_REPLACE; break;
                case 'G': cmd = IDM_EDIT_GOTO;    break;
                }
            }

            if (cmd) {
                SendMessageA(hMain, WM_COMMAND, cmd, 0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    ExitProcess((UINT)msg.wParam);
    return 0;   /* not reached */
}

/* ---------------------------------------------------------
 * main window procedure
 * --------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    /* the registered FINDMSGSTRING notification */
    if (uMsg && uMsg == uFindMsg) {
        OnFindReplaceMsg();
        return 0;
    }

    switch (uMsg) {

    case WM_CREATE:
        /* EDIT control (class "RICHEDIT50W") - size 0, resized by WM_SIZE */
        hEdit = CreateWindowExA(0, EditClass, NULL,
                                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT |
                                ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                                0, 0, 0, 0, hWnd, NULL, NULL, NULL);

        /* Rich Edit needs an event mask for EN_CHANGE notifications */
#if FEAT_LINENUMBERS
        SendMessageA(hEdit, EM_SETEVENTMASK, 0,
                     ENM_CHANGE | ENM_MOUSEEVENTS | ENM_SELCHANGE |
                     ENM_SCROLL | ENM_UPDATE);
#else
        SendMessageA(hEdit, EM_SETEVENTMASK, 0,
                     ENM_CHANGE | ENM_MOUSEEVENTS | ENM_SELCHANGE);
#endif

        /* raise Rich Edit user editing limit */
        SendMessageA(hEdit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);

        /* add Save command to system menu */
        AppendMenuA(GetSystemMenu(hWnd, FALSE), MF_STRING, IDM_SAVE, SaveText);

        /* build Notepad-style menu bar from compact tables */
        CreateNotepadMenus(hWnd);

        /* create status bar pane (STATIC control) */
        hStatus = CreateWindowExA(WS_EX_STATICEDGE, "STATIC", StatusBuf,
                                  WS_CHILD | WS_VISIBLE,
                                  0, 0, 0, 0, hWnd, NULL, NULL, NULL);

        /* show initial Ln/Col in the status bar */
        UpdateStatus();
        return 0;

#if FEAT_LINENUMBERS
    case WM_PAINT:
        /* paint the line-number gutter on the left strip */
        if (fLineNum) {
            PAINTSTRUCT ps;
            RECT        rc;
            POINT       pt;
            char        nbuf[16];
            HDC         hdc;
            LRESULT     line, count;

            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rc);
            rc.right = LN_MARGIN_W;                 /* strip = {0,0,MARGIN,clientH} */
            FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
            SetBkMode(hdc, TRANSPARENT);

            line  = SendMessageA(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
            count = SendMessageA(hEdit, EM_GETLINECOUNT, 0, 0);

            for (; line < count; ++line) {
                LRESULT ch = SendMessageA(hEdit, EM_LINEINDEX, (WPARAM)line, 0);
                SendMessageA(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, ch);
                if (pt.y > rc.bottom)
                    break;
                TextOutA(hdc, LN_PAD, pt.y, nbuf,
                         wsprintfA(nbuf, "%d", (int)(line + 1)));
            }
            EndPaint(hWnd, &ps);
            return 0;
        }
        break;      /* fall through to DefWindowProc when gutter is off */
#endif

    case WM_SYSCOMMAND:
        if (wParam == IDM_SAVE) {
            SaveFile();
            return 0;
        }
        break;

    case WM_COMMAND:
        /* check for EN_CHANGE from EDIT */
        switch (HIWORD(wParam)) {
        case EN_CHANGE:
            if (!fDirty) {
                /* mark dirty and update title */
                fDirty = 1;
                ApplyTitle();
            }
            return 0;
#if FEAT_LINENUMBERS
        case EN_VSCROLL:
        case EN_UPDATE:
            LnInvalidate(hWnd);
            return 0;
#endif
        }

        /* handle top menu commands */
        switch (LOWORD(wParam)) {

        case IDM_FILE_NEW:
            if (MaybeSaveChanges())
                NewFile();
            return 0;

        case IDM_FILE_OPEN:
            if (MaybeSaveChanges() && PickOpenFile()) {
                LoadStartupFile();
                fDirty = 0;
                ApplyTitle();
            }
            return 0;

        case IDM_SAVE:
            if (CmdFile[0] != 0) {
                SaveFile();
                return 0;
            }
            /* no file name yet: fall through to Save As */

        case IDM_FILE_SAVEAS:
            if (PickSaveFile())
                SaveFile();
            return 0;

        case IDM_FILE_PRINT:
            PrintDoc();
            return 0;

        case IDM_FILE_PAGESETUP:
            PageSetup();
            return 0;

        case IDM_FILE_EXIT:
            if (MaybeSaveChanges())
                DestroyWindow(hWnd);
            return 0;

        case IDM_EDIT_UNDO:
            SendMessageA(hEdit, WM_UNDO, 0, 0);
            return 0;

        case IDM_EDIT_CUT:
            SendMessageA(hEdit, WM_CUT, 0, 0);
            return 0;

        case IDM_EDIT_COPY:
            SendMessageA(hEdit, WM_COPY, 0, 0);
            return 0;

        case IDM_EDIT_PASTE:
            SendMessageA(hEdit, WM_PASTE, 0, 0);
            return 0;

        case IDM_EDIT_DELETE:
            SendMessageA(hEdit, WM_CLEAR, 0, 0);
            return 0;

        case IDM_EDIT_SELALL:
            SetFocus(hEdit);
            SendMessageA(hEdit, EM_SETSEL, 0, -1);
            return 0;

        case IDM_EDIT_TIME:
            SetFocus(hEdit);
            InsertTimeDate();
            return 0;

        case IDM_EDIT_FIND:
            InitFR();
            hFindDlg = FindTextA(&fr);
            return 0;

        case IDM_EDIT_FINDNEXT:
            DoFindNext();
            return 0;

        case IDM_EDIT_REPLACE:
            InitFR();
            hFindDlg = ReplaceTextA(&fr);
            return 0;

        case IDM_EDIT_GOTO:
            GoToDlg();
            return 0;

        case IDM_FMT_WRAP:
            ToggleWrap();
            return 0;

        case IDM_FMT_FONT:
            ChooseFontDlg();
            return 0;

        case IDM_VIEW_STATUS:
            fStatus = !fStatus;
            ShowWindow(hStatus, fStatus ? SW_SHOW : SW_HIDE);
            RelayoutClient();
            UpdateStatus();
            return 0;

#if FEAT_LINENUMBERS
        case IDM_VIEW_LINENUM:
            fLineNum = !fLineNum;
            RelayoutClient();
            LnInvalidate(hWnd);
            return 0;
#endif

#if FEAT_DARKMODE
        case IDM_VIEW_DARK: {
            /* toggle dark mode: recolor the Rich Edit, check the menu item */
            CHARFORMATW dfmt = {0};

            fDark = !fDark;

            dfmt.cbSize = sizeof(CHARFORMATW);
            dfmt.dwMask = CFM_COLOR;

            if (fDark) {
                /* dark on: dark background + light text */
                SendMessageA(hEdit, EM_SETBKGNDCOLOR, 0, DARK_BG);
                dfmt.crTextColor = DARK_FG;
            } else {
                /* dark off: system background + auto (default) text color */
                SendMessageA(hEdit, EM_SETBKGNDCOLOR, 1, 0);
                dfmt.dwEffects = CFE_AUTOCOLOR;
            }
            SendMessageA(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&dfmt);

            /* reflect the new state with a check mark in the View menu */
            CheckMenuItem(GetMenu(hWnd), IDM_VIEW_DARK,
                          MF_BYCOMMAND | (fDark ? MF_CHECKED : 0));
            return 0;
        }
#endif

        case IDM_HELP_ABOUT:
            MessageBoxA(hWnd, AboutText, AboutCap, MB_OK | MB_ICONINFORMATION);
            return 0;

        case IDM_HELP_VIEWHELP:
            ShellExecuteA(NULL, "open", HelpUrl, NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lParam;

        if (nm->code == EN_SELCHANGE) {
            /* caret/selection moved */
            UpdateStatus();
            return 0;
        }
        if (nm->code == EN_MSGFILTER &&
            ((MSGFILTER *)nm)->msg == WM_RBUTTONUP) {
            ShowContextMenu(hWnd);
            return 1;
        }
        return 0;
    }

    case WM_SIZE: {
        /* unpack width/height from lParam */
        int w = LOWORD(lParam);         /* client width  */
        int h = HIWORD(lParam);         /* client height */
        int x = 0;

        /* if status bar shown, reserve space and place it */
        if (fStatus) {
            h -= SBHEIGHT;
            SetWindowPos(hStatus, NULL, 0, h, w, SBHEIGHT, SWP_NOZORDER);
        }

#if FEAT_LINENUMBERS
        /* shift the edit right by the gutter when line numbers are on */
        if (fLineNum) {
            x  = LN_MARGIN_W;
            w -= LN_MARGIN_W;
        }
        SetWindowPos(hEdit, NULL, x, 0, w, h, SWP_NOZORDER);
        LnInvalidate(hWnd);
#else
        /* resize EDIT to fill remaining area */
        SetWindowPos(hEdit, NULL, x, 0, w, h, SWP_NOZORDER);
#endif
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    /* default message handling */
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}
