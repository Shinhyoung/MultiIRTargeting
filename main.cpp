/*
 * OptiTrack Flex 13 Dual Camera IR Viewer
 *
 * - 두 카메라를 하나의 창에 좌우로 나란히 출력
 * - 각 카메라 영역에서 마우스로 4개 코너 포인트 선택
 * - 두 카메라 모두 4점 선택 완료 시 호모그래피 변환 후 이어 붙인 워프 창 자동 표시
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include "settings.h"
#include "homography.h"
#include "udp_sender.h"
#include "frame_processor.h"
#include "osd_renderer.h"
#include "config_manager.h"

#include <cstdint>
#include "cameralibrary.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <string>

using namespace CameraLibrary;

// ─────── OSD helper ──────────────────────────────────────
static void putShadow(cv::Mat& img, const std::string& t, cv::Scalar color, cv::Point pos)
{
    cv::putText(img, t, pos + cv::Point(1, 1),
                cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0,0,0), 2, cv::LINE_AA);
    cv::putText(img, t, pos,
                cv::FONT_HERSHEY_SIMPLEX, 0.42, color, 1, cv::LINE_AA);
}

static void drawMainOSD(cv::Mat& img,
                        bool udpOn,
                        int pts0, bool hom0Ready,
                        int pts1, bool hom1Ready,
                        bool configSaved,
                        int cam0W)
{
    // Key bindings box
    {
        cv::Mat overlay = img.clone();
        cv::rectangle(overlay, cv::Point(4, 4), cv::Point(270, 136),
                      cv::Scalar(15, 15, 15), cv::FILLED);
        cv::addWeighted(overlay, 0.65, img, 0.35, 0, img);
    }

    cv::Scalar udpColor  = udpOn ? cv::Scalar(60,255,60) : cv::Scalar(120,200,255);
    std::string udpLabel = std::string("[U] UDP: ") + (udpOn ? "ON  (Sending)" : "OFF");

    putShadow(img, "[Q/ESC] Quit",                     cv::Scalar(200,200,200), cv::Point(10, 22));
    putShadow(img, udpLabel,                            udpColor,               cv::Point(10, 40));
    putShadow(img, "[R] Reset all corners",             cv::Scalar(200,200,200), cv::Point(10, 58));
    putShadow(img, "[P] Settings",                      cv::Scalar(200,200,200), cv::Point(10, 76));
    putShadow(img, "[S] Save config",                   cv::Scalar(200,200,200), cv::Point(10, 94));
    putShadow(img, "[L-Click] Select corner (4 pts)",   cv::Scalar(200,200,200), cv::Point(10, 112));
    putShadow(img, "  left half=Cam0 / right half=Cam1", cv::Scalar(180,180,180), cv::Point(10, 130));

    // Per-camera corner selection progress (bottom of each half)
    auto c0 = hom0Ready ? cv::Scalar(60,255,60) : cv::Scalar(255,190,60);
    auto c1 = hom1Ready ? cv::Scalar(60,255,60) : cv::Scalar(255,190,60);
    std::string s0 = hom0Ready ? "Cam0: READY" : ("Cam0: " + std::to_string(pts0) + "/4 pts");
    std::string s1 = hom1Ready ? "Cam1: READY" : ("Cam1: " + std::to_string(pts1) + "/4 pts");
    cv::putText(img, s0, cv::Point(10, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, c0, 1, cv::LINE_AA);
    cv::putText(img, s1, cv::Point(cam0W + 10, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, c1, 1, cv::LINE_AA);

    // UDP status (bottom center)
    std::string udpSt = udpOn ? "udp SENDING" : "udp STOPPED";
    cv::Scalar  udpSC = udpOn ? cv::Scalar(60,255,60) : cv::Scalar(120,120,120);
    cv::putText(img, udpSt, cv::Point(img.cols / 2 - 50, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, udpSC, 1, cv::LINE_AA);

    // Config saved flash
    if (configSaved)
    {
        const std::string msg = "Config Saved!";
        cv::putText(img, msg, cv::Point(img.cols/2 - 79, 36),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,0,0), 3, cv::LINE_AA);
        cv::putText(img, msg, cv::Point(img.cols/2 - 80, 35),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(80,255,160), 2, cv::LINE_AA);
    }
}

// ─────── Main ────────────────────────────────────────────
int main(int argc, char* argv[])
{
    std::ofstream logFile("IRViewer_log.txt");
    auto cout_buf = std::cout.rdbuf(logFile.rdbuf());
    auto cerr_buf = std::cerr.rdbuf(logFile.rdbuf());
    auto restoreLog = [&]()
    {
        std::cout.rdbuf(cout_buf);
        std::cerr.rdbuf(cerr_buf);
        logFile.close();
    };

    std::cout << "=== OptiTrack Flex 13 Dual Camera IR Viewer ===" << std::endl;

    // ========== 설정 로드 ==========
    AppSettings settings;
    std::vector<cv::Point2f> cam0Corners;
    bool configLoaded = loadConfig(settings, cam0Corners, 0);
    if (!configLoaded)
        ShowSettingsDialog(settings);

    std::cout << "Settings: IP=" << settings.ipAddress
              << " Port=" << settings.port
              << " TargetW=" << settings.targetWidth
              << " TargetH=" << settings.targetHeight
              << " Exposure=" << settings.exposure << std::endl;

    // ========== Camera SDK 초기화 ==========
    CameraManager::X().WaitForInitialization();
    if (!CameraManager::X().AreCamerasInitialized())
    {
        restoreLog();
        MessageBoxA(NULL, "Failed to initialize Camera SDK.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    CameraList list;
    int numDetected = list.Count();
    std::cout << "Cameras detected: " << numDetected << std::endl;

    if (numDetected < 2)
    {
        restoreLog();
        CameraManager::X().Shutdown();
        MessageBoxA(NULL,
            "At least 2 OptiTrack cameras are required.\nCheck connections and try again.",
            "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    for (int i = 0; i < numDetected; i++)
        std::cout << "  Camera " << i << ": " << list[i].Name()
                  << "  UID=" << list[i].UID()
                  << "  State=" << list[i].State() << std::endl;

    // ========== 모든 카메라 State==6 대기 ==========
    std::cout << "Waiting for cameras to initialize..." << std::endl;
    bool allReady = false;
    for (int attempt = 0; attempt < 150; attempt++)
    {
        CameraList cur;
        if (cur.Count() >= 2)
        {
            allReady = true;
            for (int i = 0; i < cur.Count(); i++)
                if (cur[i].State() != 6) { allReady = false; break; }
            if (allReady) { std::cout << "All cameras ready." << std::endl; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!allReady)
    {
        restoreLog();
        CameraManager::X().Shutdown();
        MessageBoxA(NULL, "Camera initialization timeout.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // ========== 카메라 획득 (첫 두 대만 사용) ==========
    auto cam0 = CameraManager::X().GetCamera(list[0].UID());
    auto cam1 = CameraManager::X().GetCamera(list[1].UID());
    if (!cam0 || !cam1)
    {
        restoreLog();
        CameraManager::X().Shutdown();
        MessageBoxA(NULL, "Failed to acquire cameras.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    for (auto& cam : { cam0, cam1 })
    {
        cam->SetVideoType(Core::GrayscaleMode);
        cam->SetExposure(settings.exposure);
        cam->SetIntensity(0);
        cam->Start();
    }

    int cam0W = cam0->Width(),  cam0H = cam0->Height();
    int cam1W = cam1->Width(),  cam1H = cam1->Height();
    int mainH = std::max(cam0H, cam1H);

    std::cout << "Cam0: " << cam0->Name() << " (" << cam0W << "x" << cam0H << ")" << std::endl;
    std::cout << "Cam1: " << cam1->Name() << " (" << cam1W << "x" << cam1H << ")" << std::endl;

    // ========== 호모그래피 상태 ==========
    HomographyState hom0, hom1;

    // Cam0 코너 복원
    if (configLoaded && (int)cam0Corners.size() == HomographyState::REQUIRED_POINTS)
    {
        hom0.selectedPoints = cam0Corners;
        float tw = (float)(settings.targetWidth  - 1);
        float th = (float)(settings.targetHeight - 1);
        std::vector<cv::Point2f> dst = { {0.f,0.f},{tw,0.f},{tw,th},{0.f,th} };
        hom0.matrix = cv::getPerspectiveTransform(hom0.selectedPoints, dst);
        hom0.ready  = true;
        std::cout << "[Config] Cam0 homography restored." << std::endl;
    }

    // Cam1 코너 복원
    {
        std::vector<cv::Point2f> cam1Corners;
        AppSettings dummy = settings;
        if (loadConfig(dummy, cam1Corners, 1) &&
            (int)cam1Corners.size() == HomographyState::REQUIRED_POINTS)
        {
            hom1.selectedPoints = cam1Corners;
            float tw = (float)(settings.targetWidth  - 1);
            float th = (float)(settings.targetHeight - 1);
            std::vector<cv::Point2f> dst = { {0.f,0.f},{tw,0.f},{tw,th},{0.f,th} };
            hom1.matrix = cv::getPerspectiveTransform(hom1.selectedPoints, dst);
            hom1.ready  = true;
            std::cout << "[Config] Cam1 homography restored." << std::endl;
        }
    }

    // ========== 윈도우 설정 ==========
    const std::string mainWin     = "IR View";
    const std::string stitchedWin = "Warped Stitched View";
    cv::namedWindow(mainWin, cv::WINDOW_AUTOSIZE | cv::WINDOW_GUI_NORMAL);

    // 복합 마우스 콜백
    CombinedMouseCallbackData mouseData;
    mouseData.cam0Width   = cam0W;
    mouseData.cam1Width   = cam1W;
    mouseData.frameHeight = mainH;
    mouseData.targetWidth  = settings.targetWidth;
    mouseData.targetHeight = settings.targetHeight;
    mouseData.hom0 = &hom0;
    mouseData.hom1 = &hom1;
    cv::setMouseCallback(mainWin, onMouseCombined, &mouseData);

    std::cout << "Instructions:" << std::endl;
    std::cout << "  [Q/ESC] Quit  [U] UDP  [R] Reset  [P] Settings  [S] Save" << std::endl;
    std::cout << "  Left-click in each camera half to select 4 corner points." << std::endl;

    // ========== UDP 초기화 ==========
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        CameraManager::X().Shutdown();
        return -1;
    }
    UDPSender sender;
    if (!sender.init(settings.ipAddress, settings.port))
    {
        WSACleanup();
        CameraManager::X().Shutdown();
        return -1;
    }
    std::cout << "UDP ready: " << settings.ipAddress << ":" << settings.port << std::endl;

    // ========== 메인 루프 ==========
    bool running         = true;
    bool continuousSend  = (configLoaded && hom0.ready && hom1.ready);
    bool stitchedWinOpen = false;
    bool showConfigSaved = false;
    auto configSavedTime = std::chrono::steady_clock::time_point{};

    if (continuousSend)
        std::cout << "[Config] Auto-started UDP streaming." << std::endl;

    cv::Mat latestGray0, latestGray1;
    FrameResult r0, r1;

    while (running)
    {
        // ── 카메라 0 프레임 처리 ──
        {
            auto frame = cam0->LatestFrame();
            if (frame && frame->IsGrayscale())
            {
                const unsigned char* data = frame->GrayscaleData(*cam0);
                if (data)
                {
                    latestGray0 = cv::Mat(cam0H, cam0W, CV_8UC1,
                                         const_cast<unsigned char*>(data)).clone();
                    r0 = processFrame(data, cam0W, cam0H, hom0, settings);
                }
            }
        }

        // ── 카메라 1 프레임 처리 ──
        {
            auto frame = cam1->LatestFrame();
            if (frame && frame->IsGrayscale())
            {
                const unsigned char* data = frame->GrayscaleData(*cam1);
                if (data)
                {
                    latestGray1 = cv::Mat(cam1H, cam1W, CV_8UC1,
                                         const_cast<unsigned char*>(data)).clone();
                    r1 = processFrame(data, cam1W, cam1H, hom1, settings);
                }
            }
        }

        // ── 메인 창 합성 ──
        cv::Mat mainView(mainH, cam0W + cam1W, CV_8UC3, cv::Scalar(20, 20, 20));

        if (!r0.leftPanel.empty())
        {
            // 검출된 블롭 중심 (초록 원) 표시
            for (const auto& c : r0.detectedCenters)
                cv::circle(r0.leftPanel, c, 5, cv::Scalar(0, 200, 0), -1);
            r0.leftPanel.copyTo(mainView(cv::Rect(0, 0, cam0W, cam0H)));
        }
        if (!r1.leftPanel.empty())
        {
            for (const auto& c : r1.detectedCenters)
                cv::circle(r1.leftPanel, c, 5, cv::Scalar(0, 200, 0), -1);
            r1.leftPanel.copyTo(mainView(cv::Rect(cam0W, 0, cam1W, cam1H)));
        }

        // 카메라 구분 헤더 바
        cv::rectangle(mainView, cv::Point(0, 0), cv::Point(cam0W, 18),
                      cv::Scalar(40, 40, 80), cv::FILLED);
        cv::rectangle(mainView, cv::Point(cam0W, 0), cv::Point(cam0W + cam1W, 18),
                      cv::Scalar(40, 80, 40), cv::FILLED);
        cv::putText(mainView, std::string("CAM 0 - ") + cam0->Name(),
                    cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(200, 200, 255), 1, cv::LINE_AA);
        cv::putText(mainView, std::string("CAM 1 - ") + cam1->Name(),
                    cv::Point(cam0W + 6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(200, 255, 200), 1, cv::LINE_AA);

        // 좌우 구분선
        cv::line(mainView, cv::Point(cam0W, 0), cv::Point(cam0W, mainH),
                 cv::Scalar(150, 150, 150), 2);

        // Config 저장 타이머
        if (showConfigSaved)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - configSavedTime).count();
            if (ms > 2000) showConfigSaved = false;
        }

        // OSD
        drawMainOSD(mainView, continuousSend,
                    (int)hom0.selectedPoints.size(), hom0.ready,
                    (int)hom1.selectedPoints.size(), hom1.ready,
                    showConfigSaved, cam0W);

        cv::imshow(mainWin, mainView);

        // ── 워프 이어붙임 창 (두 카메라 모두 준비됐을 때) ──
        if (hom0.ready && hom1.ready &&
            !latestGray0.empty() && !latestGray1.empty())
        {
            cv::Mat warped0, warped1;
            cv::warpPerspective(latestGray0, warped0, hom0.matrix,
                                cv::Size(settings.targetWidth, settings.targetHeight));
            cv::warpPerspective(latestGray1, warped1, hom1.matrix,
                                cv::Size(settings.targetWidth, settings.targetHeight));

            cv::Mat wc0, wc1;
            cv::cvtColor(warped0, wc0, cv::COLOR_GRAY2BGR);
            cv::cvtColor(warped1, wc1, cv::COLOR_GRAY2BGR);

            // 워프 좌표계 내 블롭 중심 (빨간 점)
            for (const auto& c : r0.inBoundCenters)
            {
                cv::circle(wc0, c, 6, cv::Scalar(0, 0, 255), -1);
                cv::putText(wc0,
                            "(" + std::to_string((int)c.x) + "," + std::to_string((int)c.y) + ")",
                            cv::Point((int)c.x + 8, (int)c.y - 5),
                            cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
            }
            for (const auto& c : r1.inBoundCenters)
            {
                cv::circle(wc1, c, 6, cv::Scalar(0, 0, 255), -1);
                // x 좌표는 cam1 오프셋(targetWidth) 적용하여 표시
                int dispX = (int)c.x + settings.targetWidth;
                cv::putText(wc1,
                            "(" + std::to_string(dispX) + "," + std::to_string((int)c.y) + ")",
                            cv::Point((int)c.x + 8, (int)c.y - 5),
                            cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
            }

            // 워프 창 헤더 바
            cv::rectangle(wc0, cv::Point(0,0), cv::Point(wc0.cols, 18),
                          cv::Scalar(40, 40, 80), cv::FILLED);
            cv::rectangle(wc1, cv::Point(0,0), cv::Point(wc1.cols, 18),
                          cv::Scalar(40, 80, 40), cv::FILLED);
            cv::putText(wc0, std::string("CAM 0 - ") + cam0->Name() + " [Warped]",
                        cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(200, 200, 255), 1, cv::LINE_AA);
            cv::putText(wc1, std::string("CAM 1 - ") + cam1->Name() + " [Warped]",
                        cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(200, 255, 200), 1, cv::LINE_AA);

            // 이어 붙이기
            cv::Mat stitched;
            cv::hconcat(wc0, wc1, stitched);

            // 구분선
            cv::line(stitched,
                     cv::Point(settings.targetWidth, 0),
                     cv::Point(settings.targetWidth, stitched.rows),
                     cv::Scalar(150, 150, 150), 2);

            if (!stitchedWinOpen)
            {
                cv::namedWindow(stitchedWin, cv::WINDOW_AUTOSIZE | cv::WINDOW_GUI_NORMAL);
                stitchedWinOpen = true;
                std::cout << "[Warped] Stitched view opened." << std::endl;
            }
            cv::imshow(stitchedWin, stitched);

            // UDP 전송
            // cam0: x in [0, targetWidth-1]
            // cam1: x in [targetWidth, 2*targetWidth-1]  (오프셋 적용)
            if (continuousSend)
            {
                if (!r0.inBoundCenters.empty())
                    sender.send(r0.inBoundCenters);
                if (!r1.inBoundCenters.empty())
                {
                    std::vector<cv::Point2f> offsetCenters1;
                    offsetCenters1.reserve(r1.inBoundCenters.size());
                    for (const auto& c : r1.inBoundCenters)
                        offsetCenters1.emplace_back(c.x + settings.targetWidth, c.y);
                    sender.send(offsetCenters1);
                }
            }
        }

        // ── 키 입력 ──
        int key = cv::waitKey(1);

        if (key == 113 || key == 81 || key == 27)  // q Q ESC
        {
            running = false;
        }
        else if (key == 114 || key == 82)  // r R
        {
            hom0.reset();
            hom1.reset();
            std::cout << "All corners reset." << std::endl;
        }
        else if (key == 117 || key == 85)  // u U
        {
            continuousSend = !continuousSend;
            std::cout << "[UDP] " << (continuousSend ? "ON" : "OFF") << std::endl;
        }
        else if (key == 115 || key == 83)  // s S
        {
            bool ok = saveConfig(settings, hom0.selectedPoints, 0) &&
                      saveConfig(settings, hom1.selectedPoints, 1);
            if (ok)
            {
                showConfigSaved = true;
                configSavedTime = std::chrono::steady_clock::now();
            }
        }
        else if (key == 112 || key == 80)  // p P
        {
            AppSettings prev = settings;
            if (ShowSettingsDialog(settings))
            {
                if (settings.exposure != prev.exposure)
                {
                    cam0->SetExposure(settings.exposure);
                    cam1->SetExposure(settings.exposure);
                    std::cout << "[Settings] Exposure -> " << settings.exposure << std::endl;
                }
                if (strcmp(settings.ipAddress, prev.ipAddress) != 0 ||
                    settings.port != prev.port)
                    sender.updateTarget(settings.ipAddress, settings.port);
                if (settings.targetWidth  != prev.targetWidth ||
                    settings.targetHeight != prev.targetHeight)
                {
                    mouseData.targetWidth  = settings.targetWidth;
                    mouseData.targetHeight = settings.targetHeight;
                    hom0.reset();
                    hom1.reset();
                    std::cout << "[Settings] Target res changed -> all corners reset." << std::endl;
                }
            }
        }
    }

    // ========== 정리 ==========
    cv::destroyAllWindows();
    WSACleanup();
    CameraManager::X().Shutdown();
    std::cout << "Program terminated." << std::endl;
    restoreLog();
    return 0;
}
