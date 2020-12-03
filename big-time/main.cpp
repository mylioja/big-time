//  An extremely stylish screensaver with elegant hour and minute display
//
//  Copyright (C) 2020  Martti Ylioja
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#define UNICODE

#include <windows.h>
#include <regstr.h>
#include <scrnsave.h>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ScrnSavw.lib")

namespace {

    //
    //  Only one single screen supported, so all data can be global
    //

    HANDLE hPrivateFont = 0;
    UINT_PTR uTimer = 0;
    HBRUSH hBackgroundBrush = 0;
    HFONT hFont = 0;
    COLORREF bkColor = RGB(0, 20, 35);
    COLORREF textColor = RGB(80, 80, 80);

    bool init_done = false;
    int hour_and_minute = -1;

    //  The default font is "Abril Fatface" and it's embedded into the binary as a font resource.
    //  Font Copyright(C) 2011, TypeTogether(http://www.type-together.com info@type-together.com)
    //  The font is licensed under the SIL Open Font License, Version 1.1,
    //  available with a FAQ at: http://scripts.sil.org/OFL
    //
    const WCHAR defaultFont[] = L"AbrilFatface-Regular";

    const WCHAR regSubkey[] = L"Software\\ylioja.com\\big-time.scr";
    const WCHAR regBkColor[] = L"bkcolor";
    const WCHAR regTextColor[] = L"textcolor";
    const WCHAR regTextFont[] = L"font";

    LOGFONT logfont = {
        -500, 0,
        0, 0,
        FW_BOLD,
        FALSE, FALSE, FALSE,
        ANSI_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH,
        L""
    };


    void delete_object(HANDLE& obj)
    {
        if (obj && obj != INVALID_HANDLE_VALUE)
        {
            DeleteObject(obj);
            obj = 0;
        }
    }

    void delete_object(HFONT& obj)
    {
        delete_object(*(HANDLE*)(&obj));
    }


    //  Load a DWORD from the registry
    DWORD reg_load_dword_value(LPCWSTR name, DWORD default_value)
    {
        DWORD data;
        DWORD size = sizeof data;
        LSTATUS stat = RegGetValue(
            HKEY_CURRENT_USER, regSubkey, name, RRF_RT_REG_DWORD,
            nullptr, &data, &size
        );

        if (stat != ERROR_SUCCESS)
        {
            return default_value;
        }

        return data;
    }


    //  Store a DWORD to the registry
    void reg_store_dword_value(LPCWSTR name, DWORD data)
    {
        RegSetKeyValue(HKEY_CURRENT_USER, regSubkey, name, REG_DWORD, &data, sizeof data);
    }


    //  Load a text value from the registry
    void reg_load_text_value(LPCWSTR name, LPWSTR buffer, DWORD buffer_size)
    {
        LSTATUS stat = RegGetValue(
            HKEY_CURRENT_USER, regSubkey, name, RRF_RT_REG_SZ | RRF_ZEROONFAILURE,
            nullptr, buffer, &buffer_size
        );
    }


    //  Store a text value to the registry
    void reg_store_text_value(LPCWSTR name, LPCWSTR data)
    {
        RegSetKeyValue(HKEY_CURRENT_USER, regSubkey, name, REG_SZ, data, 2*(wcslen(data)+1));
    }


    //  Read config data from the registry
    void read_config()
    {
        bkColor = reg_load_dword_value(regBkColor, bkColor);
        textColor = reg_load_dword_value(regTextColor, textColor);

        //  Font name
        reg_load_text_value(regTextFont, logfont.lfFaceName, sizeof logfont.lfFaceName);
        if (logfont.lfFaceName[0] == 0)
        {
            memcpy(logfont.lfFaceName, defaultFont, 2*(wcslen(defaultFont)+1));
        }
    }


    //  Write config data to the registry
    void write_config()
    {
        reg_store_dword_value(regBkColor, bkColor);
        reg_store_dword_value(regTextColor, textColor);
        reg_store_text_value(regTextFont, logfont.lfFaceName);
    }


    //  Load the private (default) font from resource
    void load_private_font()
    {
        HINSTANCE hInstance = ::GetModuleHandle(nullptr);
        HRSRC hFontResource = FindResource(hInstance, MAKEINTRESOURCE(IDF_FONT1), L"BINARY");
        if (hFontResource)
        {
            HGLOBAL hFontMemory = LoadResource(hInstance, hFontResource);
            if (hFontMemory != nullptr)
            {
                DWORD count = 0;
                DWORD size = SizeofResource(hInstance, hFontResource);
                void* data = LockResource(hFontMemory);
                hPrivateFont = AddFontMemResourceEx(data, size, nullptr, &count);
                //
                //  There's no reason to unlock the font memory, or worry about
                //  calling RemoveFontMemResourceEx at the end.
                //  Process termination will handle it all.
                //
            }
        }
    }


    //  Create a font with initial arbitrary size
    void create_font()
    {
        load_private_font();
        hFont = CreateFontIndirect(&logfont);
    }


    //  Resize the font to fit the current screen dimensions
    void resize_font(HWND hWnd)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        HDC hdc = GetDC(hWnd);
        HGDIOBJ hOldFont = SelectObject(hdc, hFont);

        SIZE size;
        GetTextExtentPoint32(hdc, L"00:00", 5, &size);

        int maxh = -(height * logfont.lfHeight) / size.cy;
        int maxw = -(width * logfont.lfHeight) / size.cx;
        if (maxw < maxh)
        {
            maxh = maxw;
        }

        SelectObject(hdc, hOldFont);
        delete_object(hFont);
        logfont.lfHeight = -maxh;
        hFont = CreateFontIndirect(&logfont);
    }


    //  WM_CREATE
    void on_create(HWND hWnd)
    {
        read_config();
        create_font();
        hBackgroundBrush = CreateSolidBrush(bkColor);
    }


    //  WM_ERASEBKGND
    void on_erasebkgnd(HWND hWnd)
    {
        if (init_done)
        {
            return;
        }

        resize_font(hWnd);
        init_done = true;

        uTimer = SetTimer(hWnd, 1, 1000, NULL);
        PostMessage(hWnd, WM_TIMER, 0, 0);
    }


    //  WM_TIMER - called once every second
    void on_timer(HWND hWnd)
    {
        //  Return immediately if still the same minute
        SYSTEMTIME st;
        GetLocalTime(&st);
        int hhmm = 100*st.wHour + st.wMinute;
        if (hhmm == hour_and_minute)
        {
            return;
        }

        //  Minute has changed
        hour_and_minute = hhmm;

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        HDC hdc = GetDC(hWnd);
        HDC hMemDc = CreateCompatibleDC(hdc);
        HBITMAP membitmap = CreateCompatibleBitmap(hdc, width, height);
        SelectObject(hMemDc, membitmap);

        FillRect(hMemDc, &rc, hBackgroundBrush);

        SetBkMode(hMemDc, TRANSPARENT);
        SetTextColor(hMemDc, textColor);
        SelectObject(hMemDc, hFont);

        WCHAR time[12];
        wsprintf(time, L"%02d:%02d", st.wHour, st.wMinute);
        DrawText(hMemDc, time, -1, &rc, DT_CENTER | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);

        BitBlt(hdc, 0, 0, width, height, hMemDc, 0, 0, SRCCOPY);
        DeleteObject(membitmap);
        DeleteDC(hMemDc);

        ReleaseDC(hWnd, hdc);
    }


    //  WM_DESTROY
    void on_destroy(HWND hWnd)
    {
        if (uTimer)
        {
            KillTimer(hWnd, uTimer);
        }

        delete_object(hFont);
    }


    //  Open the standard "color chooser dialog" for color selection
    COLORREF choose_color(HWND hDlg, COLORREF color)
    {
        CHOOSECOLOR cc = {};
        COLORREF customColors[16] = {};

        cc.lStructSize = sizeof cc;
        cc.hwndOwner = hDlg;
        cc.lpCustColors = (LPDWORD)customColors;
        cc.rgbResult = color;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;

        if (!ChooseColor(&cc))
        {
            //  Something went wrong, so return the original color
            return color;
        }

        return cc.rgbResult;
    }


    //  Callback for enumerating available fonts
    int CALLBACK enum_callback(CONST LOGFONTW* lf, CONST TEXTMETRICW *, DWORD type, LPARAM lparam)
    {
        //  Accept only truetype fonts
        if ((type & TRUETYPE_FONTTYPE) == 0)
        {
            return TRUE;
        }

        //  Skip vertical fonts (name begins with a '@')
        if (lf->lfFaceName[0] == '@')
        {
            return TRUE;
        }

        //  The names are always zero terminated
        SendMessage((HWND)lparam, LB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);

        return TRUE;
    }


    //  Fill the list of available fonts
    void populate_font_names_list(HWND hDlg)
    {
        HWND hList = GetDlgItem(hDlg, IDC_LIST1);

        //  Add the private initial default font
        SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)defaultFont);

        LOGFONT lf = {};
        lf.lfCharSet = ANSI_CHARSET;

        HDC hdc = GetDC(nullptr);
        EnumFontFamiliesEx(hdc, &lf, enum_callback, (LPARAM)hList, 0);
        ReleaseDC(nullptr, hdc);

        LRESULT pos = SendMessage(hList, LB_FINDSTRINGEXACT, -1, (LPARAM)logfont.lfFaceName);
        SendMessage(hList, LB_SETCURSEL, pos, 0);
    }


    //  Get name of the selected font
    void get_selected_font(HWND hDlg)
    {
        HWND hList = GetDlgItem(hDlg, IDC_LIST1);
        LRESULT sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR)
        {
            return;
        }

        //  The size from LB_GETTEXTLEN may be inaccurate, so prepare to read a bit more.
        const static int bufferSize = 10 + sizeof logfont.lfFaceName / sizeof logfont.lfFaceName[0];
        WCHAR name[bufferSize];
        LRESULT size = SendMessage(hList, LB_GETTEXTLEN, sel, 0);
        if (size == LB_ERR || size >= bufferSize)
        {
            return;
        }

        //  Now we get the exact size (count of characters without the terminating zero).
        size = SendMessage(hList, LB_GETTEXT, sel, (LPARAM)name);
        if (size == LB_ERR || size >= sizeof logfont.lfFaceName / sizeof logfont.lfFaceName[0])
        {
            return;
        }

        ZeroMemory(logfont.lfFaceName, sizeof logfont.lfFaceName);
        memcpy(logfont.lfFaceName, name, 2*size);
    }

}   // namespace


//  This is the main "WindowProc" of the screensaver
LRESULT WINAPI ScreenSaverProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        on_create(hWnd);
        break;

    case WM_ERASEBKGND:
        on_erasebkgnd(hWnd);
        break;

    case WM_TIMER:
        on_timer(hWnd);
        break;

    case WM_DESTROY:
        on_destroy(hWnd);
        break;

    default:
        break;
    }

    return DefScreenSaverProc(hWnd, message, wParam, lParam);
}


BOOL WINAPI ScreenSaverConfigureDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HANDLE temp_brush = 0;

    switch (message)
    {
    case WM_INITDIALOG:
        temp_brush = 0;
        read_config();
        populate_font_names_list(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
        {
            bool ok = (LOWORD(wParam) == IDOK);
            if (ok)
            {
                //  Save the possibly updated values
                get_selected_font(hDlg);
                write_config();
            }

            EndDialog(hDlg, ok);
            delete_object(temp_brush);
            return TRUE;
        }

        case IDC_BUTTON1:   //  Change backgroung color
            bkColor = choose_color(hDlg, bkColor);
            InvalidateRect(GetDlgItem(hDlg, IDC_STATIC3), nullptr, TRUE);
            return TRUE;

        case IDC_BUTTON2:   //  Change text color
            textColor = choose_color(hDlg, textColor);
            InvalidateRect(GetDlgItem(hDlg, IDC_STATIC4), nullptr, TRUE);
            return TRUE;

        default:
            break;
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        int ix = GetDlgCtrlID((HWND)lParam);
        if (ix == IDC_STATIC3 || ix == IDC_STATIC4)
        {
            //  Show a small sample patch of the color in question
            delete_object(temp_brush);
            temp_brush = CreateSolidBrush(ix == IDC_STATIC3 ? bkColor : textColor);
            return (BOOL)temp_brush;
        }
        break;
    }

    default:
        break;
    }

    return FALSE;
}


BOOL WINAPI RegisterDialogClasses(HANDLE hInst)
{
    return TRUE;
}
