#pragma once

// WIN32_LEAN_AND_MEAN: winsock 충돌 방지
// NOMINMAX: windows.h의 min/max 매크로가 std::min/std::max를 오염시키는 것 방지
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ========== 앱 설정 구조체 ==========
struct AppSettings
{
    char ipAddress[64];
    int  port;
    int  targetWidth;
    int  targetHeight;
    int  exposure;
    int  udpFps;        // UDP 전송 속도 제한 (fps), 0 = 제한 없음

    AppSettings()
    {
        strcpy_s(ipAddress, sizeof(ipAddress), "127.0.0.1");
        port         = 7777;
        targetWidth  = 1024;
        targetHeight = 768;
        exposure     = 7500;
        udpFps       = 60;
    }
};

// true = OK 눌림 / false = Cancel 또는 창 닫기
bool ShowSettingsDialog(AppSettings& settings);
