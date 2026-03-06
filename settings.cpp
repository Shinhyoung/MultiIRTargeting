#include "settings.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// ========== 설정 다이얼로그 컨트롤 ID ==========
#define IDC_SETT_IP       201
#define IDC_SETT_PORT     202
#define IDC_SETT_WIDTH    203
#define IDC_SETT_HEIGHT   204
#define IDC_SETT_EXPOSURE 205
#define IDC_SETT_UDPFPS   206

// 파일 static: 다이얼로그 내부에서만 사용
static HWND         g_hIp, g_hPort, g_hWidth, g_hHeight, g_hExposure, g_hUdpFps;
static AppSettings* g_pDlgSettings = nullptr;
static int          g_dlgResult    = 0; // 0 = Cancel/닫기, 1 = OK

static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_pDlgSettings = (AppSettings*)((CREATESTRUCT*)lParam)->lpCreateParams;
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        const int margin = 12, labelW = 170, editW = 150, editH = 22, rowH = 32;
        int y = margin;

        auto makeLabel = [&](LPCSTR text, int yy)
        {
            HWND h = CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE,
                margin, yy + 3, labelW, 18, hwnd, nullptr,
                GetModuleHandleA(nullptr), nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        };

        auto makeEdit = [&](LPCSTR text, int id, int yy, bool numOnly) -> HWND
        {
            DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
            if (numOnly) style |= ES_NUMBER;
            HWND h = CreateWindowA("EDIT", text, style,
                margin + labelW + 8, yy, editW, editH, hwnd,
                (HMENU)(UINT_PTR)id, GetModuleHandleA(nullptr), nullptr);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            return h;
        };

        char buf[64];

        makeLabel("IP Address:", y);
        g_hIp = makeEdit(g_pDlgSettings->ipAddress, IDC_SETT_IP, y, false); y += rowH;

        makeLabel("Port:", y);
        sprintf_s(buf, "%d", g_pDlgSettings->port);
        g_hPort = makeEdit(buf, IDC_SETT_PORT, y, true); y += rowH;

        makeLabel("Target Width (px):", y);
        sprintf_s(buf, "%d", g_pDlgSettings->targetWidth);
        g_hWidth = makeEdit(buf, IDC_SETT_WIDTH, y, true); y += rowH;

        makeLabel("Target Height (px):", y);
        sprintf_s(buf, "%d", g_pDlgSettings->targetHeight);
        g_hHeight = makeEdit(buf, IDC_SETT_HEIGHT, y, true); y += rowH;

        makeLabel("Exposure (0 ~ 7500):", y);
        sprintf_s(buf, "%d", g_pDlgSettings->exposure);
        g_hExposure = makeEdit(buf, IDC_SETT_EXPOSURE, y, true); y += rowH;

        makeLabel("UDP TX Rate (1 ~ 1000 fps):", y);
        sprintf_s(buf, "%d", g_pDlgSettings->udpFps);
        g_hUdpFps = makeEdit(buf, IDC_SETT_UDPFPS, y, true); y += rowH + 10;

        // OK / Cancel 버튼
        const int btnW = 80, btnH = 28, btnGap = 12;
        const int clientW = margin * 2 + labelW + 8 + editW;
        int startX = (clientW - btnW * 2 - btnGap) / 2;

        HWND hOk = CreateWindowA("BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            startX, y, btnW, btnH, hwnd, (HMENU)IDOK,
            GetModuleHandleA(nullptr), nullptr);
        HWND hCancel = CreateWindowA("BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            startX + btnW + btnGap, y, btnW, btnH, hwnd, (HMENU)IDCANCEL,
            GetModuleHandleA(nullptr), nullptr);
        SendMessage(hOk,     WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            char buf[64];

            GetWindowTextA(g_hIp, g_pDlgSettings->ipAddress,
                           sizeof(g_pDlgSettings->ipAddress));

            GetWindowTextA(g_hPort, buf, sizeof(buf));
            int p = atoi(buf);
            if (p > 0 && p <= 65535) g_pDlgSettings->port = p;

            GetWindowTextA(g_hWidth, buf, sizeof(buf));
            int w = atoi(buf);
            if (w > 0) g_pDlgSettings->targetWidth = w;

            GetWindowTextA(g_hHeight, buf, sizeof(buf));
            int h = atoi(buf);
            if (h > 0) g_pDlgSettings->targetHeight = h;

            GetWindowTextA(g_hExposure, buf, sizeof(buf));
            int e = atoi(buf);
            g_pDlgSettings->exposure = std::max(0, std::min(7500, e));

            GetWindowTextA(g_hUdpFps, buf, sizeof(buf));
            int fps = atoi(buf);
            g_pDlgSettings->udpFps = std::max(1, std::min(1000, fps));

            g_dlgResult = 1;
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            g_dlgResult = 0;
            DestroyWindow(hwnd);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(g_dlgResult);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool ShowSettingsDialog(AppSettings& settings)
{
    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSA wc     = {};
        wc.lpfnWndProc   = SettingsDlgProc;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.lpszClassName = "IRViewerSettingsDlg";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassA(&wc);
        classRegistered = true;
    }

    // 클라이언트 영역 크기: margin=12, labelW=170, editW=150, gap=8
    // clientW = 12+170+8+150+12 = 352
    // clientH = 12 + 6*32 + 10 + 28 + 12 = 254
    RECT rc = { 0, 0, 352, 254 };
    DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    int dlgW = rc.right  - rc.left;
    int dlgH = rc.bottom - rc.top;
    int x    = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int y    = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    g_dlgResult = 0;
    HWND hwnd = CreateWindowExA(
        exStyle, "IRViewerSettingsDlg", "IRViewer - Settings",
        style, x, y, dlgW, dlgH,
        nullptr, nullptr, GetModuleHandleA(nullptr), &settings);
    if (!hwnd) return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG dlgMsg = {};
    while (GetMessage(&dlgMsg, nullptr, 0, 0) > 0)
    {
        if (!IsWindow(hwnd) || !IsDialogMessage(hwnd, &dlgMsg))
        {
            TranslateMessage(&dlgMsg);
            DispatchMessage(&dlgMsg);
        }
    }
    return static_cast<int>(dlgMsg.wParam) == 1;
}
