/*
 * OptiTrack Flex 13 Multi-Camera IR Viewer
 *
 * 스레드 구조:
 *   TX Thread   : 캡처 + processFrame + UDP 전송  (카메라 프레임 레이트로 독립 동작)
 *   Main Thread : 렌더링 + 키 입력               (waitKey 루프)
 *
 * 슬롯 선택:
 *   [W]  두 슬롯 교체
 *   [[]  좌 슬롯 카메라 순환
 *   []]  우 슬롯 카메라 순환
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
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

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
                        int cam0W,
                        int numCams,
                        int slot0, int slot1)
{
    {
        cv::Mat overlay = img.clone();
        int boxH = (numCams > 2) ? 172 : 154;
        cv::rectangle(overlay, cv::Point(4, 4), cv::Point(310, boxH),
                      cv::Scalar(15, 15, 15), cv::FILLED);
        cv::addWeighted(overlay, 0.65, img, 0.35, 0, img);
    }

    cv::Scalar udpColor  = udpOn ? cv::Scalar(60,255,60) : cv::Scalar(120,200,255);
    std::string udpLabel = std::string("[U] UDP: ") + (udpOn ? "ON  (Sending)" : "OFF");

    int y = 22;
    putShadow(img, "[Q/ESC] Quit",                        cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    putShadow(img, udpLabel,                               udpColor,               cv::Point(10, y)); y += 18;
    putShadow(img, "[R] Reset corners (current slots)",    cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    putShadow(img, "[P] Settings",                         cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    putShadow(img, "[S] Save config (all cameras)",        cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    putShadow(img, "[W] Swap left / right slot",           cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    if (numCams > 2)
    {
        putShadow(img, "[[] Cycle left slot  []] Cycle right slot",
                  cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    }
    putShadow(img, "[L-Click] Select corner (4 pts)",      cv::Scalar(200,200,200), cv::Point(10, y)); y += 18;
    putShadow(img, "  left half=slot0 / right half=slot1", cv::Scalar(180,180,180), cv::Point(10, y));

    auto c0 = hom0Ready ? cv::Scalar(60,255,60) : cv::Scalar(255,190,60);
    auto c1 = hom1Ready ? cv::Scalar(60,255,60) : cv::Scalar(255,190,60);
    std::string s0 = hom0Ready
        ? "Cam" + std::to_string(slot0) + ": READY"
        : "Cam" + std::to_string(slot0) + ": " + std::to_string(pts0) + "/4 pts";
    std::string s1 = hom1Ready
        ? "Cam" + std::to_string(slot1) + ": READY"
        : "Cam" + std::to_string(slot1) + ": " + std::to_string(pts1) + "/4 pts";
    cv::putText(img, s0, cv::Point(10, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, c0, 1, cv::LINE_AA);
    cv::putText(img, s1, cv::Point(cam0W + 10, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, c1, 1, cv::LINE_AA);

    std::string udpSt = udpOn ? "udp SENDING" : "udp STOPPED";
    cv::Scalar  udpSC = udpOn ? cv::Scalar(60,255,60) : cv::Scalar(120,120,120);
    cv::putText(img, udpSt, cv::Point(img.cols / 2 - 50, img.rows - 10),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, udpSC, 1, cv::LINE_AA);

    if (configSaved)
    {
        const std::string msg = "Config Saved!";
        cv::putText(img, msg, cv::Point(img.cols/2 - 79, 36),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,0,0), 3, cv::LINE_AA);
        cv::putText(img, msg, cv::Point(img.cols/2 - 80, 35),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(80,255,160), 2, cv::LINE_AA);
    }
}

// ─────── X 버튼 비활성화 ─────────────────────────────────
static void disableCloseButton(const std::string& winName)
{
    HWND hwnd = FindWindowA(NULL, winName.c_str());
    if (!hwnd) return;
    HMENU hmenu = GetSystemMenu(hwnd, FALSE);
    if (hmenu)
        EnableMenuItem(hmenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
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

    std::cout << "=== OptiTrack Flex 13 Multi-Camera IR Viewer ===" << std::endl;

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

    // ========== 전체 카메라 State==6 대기 ==========
    std::cout << "Waiting for cameras to initialize..." << std::endl;
    {
        bool allReady = false;
        for (int attempt = 0; attempt < 150; attempt++)
        {
            CameraList cur;
            if (cur.Count() >= numDetected)
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
    }

    // ========== 전체 카메라 획득 및 시작 ==========
    int numCams = numDetected;
    std::vector<std::shared_ptr<Camera>> allCams(numCams);
    std::vector<int> camW(numCams), camH(numCams);

    for (int i = 0; i < numCams; i++)
    {
        allCams[i] = CameraManager::X().GetCamera(list[i].UID());
        if (!allCams[i])
        {
            restoreLog();
            CameraManager::X().Shutdown();
            MessageBoxA(NULL, "Failed to acquire cameras.", "Error", MB_OK | MB_ICONERROR);
            return -1;
        }
        allCams[i]->SetVideoType(Core::GrayscaleMode);
        allCams[i]->SetExposure(settings.exposure);
        allCams[i]->SetIntensity(0);
        allCams[i]->Start();
        camW[i] = allCams[i]->Width();
        camH[i] = allCams[i]->Height();
        std::cout << "Cam" << i << ": " << allCams[i]->Name()
                  << " (" << camW[i] << "x" << camH[i] << ")" << std::endl;
    }

    // ========== 호모그래피 상태 (카메라별, 메인 스레드 소유) ==========
    std::vector<HomographyState> homStates(numCams);

    for (int i = 0; i < numCams; i++)
    {
        std::vector<cv::Point2f> corners;
        bool loaded = false;
        if (i == 0)
        {
            loaded = configLoaded &&
                     (int)cam0Corners.size() == HomographyState::REQUIRED_POINTS;
            if (loaded) corners = cam0Corners;
        }
        else
        {
            AppSettings dummy = settings;
            loaded = loadConfig(dummy, corners, i);
        }
        if (loaded && (int)corners.size() == HomographyState::REQUIRED_POINTS)
        {
            homStates[i].selectedPoints = corners;
            float tw = (float)(settings.targetWidth  - 1);
            float th = (float)(settings.targetHeight - 1);
            std::vector<cv::Point2f> dst = { {0.f,0.f},{tw,0.f},{tw,th},{0.f,th} };
            homStates[i].matrix = cv::getPerspectiveTransform(homStates[i].selectedPoints, dst);
            homStates[i].ready  = true;
            std::cout << "[Config] Cam" << i << " homography restored." << std::endl;
        }
    }

    // ========== 슬롯 설정 ==========
    int slot0 = 0, slot1 = 1;

    CombinedMouseCallbackData mouseData;
    auto updateMouseData = [&]()
    {
        mouseData.cam0Width    = camW[slot0];
        mouseData.cam1Width    = camW[slot1];
        mouseData.frameHeight  = std::max(camH[slot0], camH[slot1]);
        mouseData.targetWidth  = settings.targetWidth;
        mouseData.targetHeight = settings.targetHeight;
        mouseData.hom0 = &homStates[slot0];
        mouseData.hom1 = &homStates[slot1];
    };
    updateMouseData();

    // ========== 윈도우 설정 ==========
    const std::string mainWin     = "IR View";
    const std::string stitchedWin = "Warped Stitched View";
    cv::namedWindow(mainWin, cv::WINDOW_AUTOSIZE | cv::WINDOW_GUI_NORMAL);
    cv::setMouseCallback(mainWin, onMouseCombined, &mouseData);
    {
        cv::Mat dummy(10, 10, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::imshow(mainWin, dummy);
        cv::waitKey(10);
    }
    disableCloseButton(mainWin);

    std::cout << "Keys: [Q/ESC] Quit  [U] UDP  [R] Reset  [P] Settings  [S] Save" << std::endl;
    std::cout << "      [W] Swap  [[] Left slot  []] Right slot" << std::endl;
    std::cout << "      Left-click in camera half to select 4 corner points." << std::endl;

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

    // ========== 스레드 간 공유 상태 ==========
    // frameMutex  : latestGray[], results[] 보호
    // homSyncMutex: txHomStates[] 보호 (TX가 읽는 hom 복사본)
    // senderMutex : UDPSender 보호
    std::mutex frameMutex;
    std::mutex homSyncMutex;
    std::mutex senderMutex;

    std::vector<cv::Mat>     latestGray(numCams);
    std::vector<FrameResult> results(numCams);
    std::vector<HomographyState> txHomStates = homStates;  // TX 전용 복사본

    bool initSend = (configLoaded && homStates[slot0].ready && homStates[slot1].ready);
    std::atomic<bool>  atomRunning{true};
    std::atomic<bool>  atomSend{initSend};
    std::atomic<int>   atomSlot0{slot0}, atomSlot1{slot1};
    std::atomic<float> atomTxFps{0.0f};
    std::atomic<int>   atomUdpFps{settings.udpFps};

    // ========== TX 스레드 ==========
    std::thread txThread([&]()
    {
        int  txLocalCount = 0;
        auto txTimer      = std::chrono::steady_clock::now();
        auto lastSendTime = std::chrono::steady_clock::now();
        std::vector<HomographyState> localHom(numCams);

        while (atomRunning.load(std::memory_order_relaxed))
        {
            // hom 스냅샷 (메인 스레드가 waitKey 후 동기화)
            {
                std::lock_guard<std::mutex> lk(homSyncMutex);
                localHom = txHomStates;
            }

            int s0 = atomSlot0.load();
            int s1 = atomSlot1.load();

            // ── 전체 카메라 캡처 + 처리 ──
            for (int i = 0; i < numCams; i++)
            {
                auto frame = allCams[i]->LatestFrame();
                if (!frame || !frame->IsGrayscale()) continue;
                const unsigned char* data = frame->GrayscaleData(*allCams[i]);
                if (!data) continue;

                cv::Mat gray(camH[i], camW[i], CV_8UC1, const_cast<unsigned char*>(data));
                FrameResult res = processFrame(data, camW[i], camH[i], localHom[i], settings);

                std::lock_guard<std::mutex> lk(frameMutex);
                latestGray[i] = gray.clone();
                results[i]    = std::move(res);
            }

            // ── UDP 전송 (fps 제한 적용) ──
            int  targetFps      = atomUdpFps.load(std::memory_order_relaxed);
            long targetIntervalUs = (targetFps > 0) ? (1000000L / targetFps) : 0;
            auto nowSend = std::chrono::steady_clock::now();
            long elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                 nowSend - lastSendTime).count();
            bool sendDue = (targetIntervalUs == 0 || elapsedUs >= targetIntervalUs);

            if (sendDue && atomSend.load(std::memory_order_relaxed) &&
                localHom[s0].ready && localHom[s1].ready)
            {
                std::vector<cv::Point2f> c0, c1;
                {
                    std::lock_guard<std::mutex> lk(frameMutex);
                    c0 = results[s0].inBoundCenters;
                    c1 = results[s1].inBoundCenters;
                }
                lastSendTime = nowSend;
                if (!c0.empty())
                {
                    std::lock_guard<std::mutex> lk(senderMutex);
                    sender.send(c0);
                    txLocalCount++;
                }
                if (!c1.empty())
                {
                    int tw = settings.targetWidth;
                    std::vector<cv::Point2f> off1;
                    off1.reserve(c1.size());
                    for (const auto& c : c1) off1.emplace_back(c.x + tw, c.y);
                    std::lock_guard<std::mutex> lk(senderMutex);
                    sender.send(off1);
                    txLocalCount++;
                }
            }

            // ── TX FPS 갱신 (500ms 주기) ──
            auto nowTx = std::chrono::steady_clock::now();
            auto msTx  = std::chrono::duration_cast<std::chrono::milliseconds>(
                             nowTx - txTimer).count();
            if (msTx >= 500)
            {
                atomTxFps.store(txLocalCount * 1000.0f / msTx);
                txLocalCount = 0;
                txTimer      = nowTx;
            }
        }
    });

    if (initSend)
        std::cout << "[Config] Auto-started UDP streaming." << std::endl;

    // ========== Display(메인) 루프 ==========
    bool continuousSend    = initSend;
    bool stitchedWinOpen       = false;
    bool stitchedCloseDisabled = false;
    bool showConfigSaved = false;
    auto configSavedTime = std::chrono::steady_clock::time_point{};

    int   loopCount = 0;
    float loopFps   = 0.0f;
    auto  loopTimer = std::chrono::steady_clock::now();

    while (true)
    {
        // ── 프레임 스냅샷 (TX 스레드와 공유) ──
        cv::Mat      gray0, gray1;
        FrameResult  r0, r1;
        {
            std::lock_guard<std::mutex> lk(frameMutex);
            if (!latestGray[slot0].empty()) gray0 = latestGray[slot0].clone();
            if (!latestGray[slot1].empty()) gray1 = latestGray[slot1].clone();
            r0 = results[slot0];   // Mat은 ref-count 복사 → 안전
            r1 = results[slot1];
        }

        // leftPanel 클론 (위에 circle 그릴 때 TX 데이터와 분리)
        cv::Mat panel0 = r0.leftPanel.empty() ? cv::Mat() : r0.leftPanel.clone();
        cv::Mat panel1 = r1.leftPanel.empty() ? cv::Mat() : r1.leftPanel.clone();

        HomographyState& hom0 = homStates[slot0];
        HomographyState& hom1 = homStates[slot1];
        int s0W = camW[slot0], s0H = camH[slot0];
        int s1W = camW[slot1], s1H = camH[slot1];
        int mainH = std::max(s0H, s1H);

        // ── 메인 창 합성 ──
        cv::Mat mainView(mainH, s0W + s1W, CV_8UC3, cv::Scalar(20, 20, 20));

        if (!panel0.empty())
        {
            for (const auto& c : r0.detectedCenters)
                cv::circle(panel0, c, 5, cv::Scalar(0, 200, 0), -1);
            panel0.copyTo(mainView(cv::Rect(0, 0, s0W, s0H)));
        }
        if (!panel1.empty())
        {
            for (const auto& c : r1.detectedCenters)
                cv::circle(panel1, c, 5, cv::Scalar(0, 200, 0), -1);
            panel1.copyTo(mainView(cv::Rect(s0W, 0, s1W, s1H)));
        }

        cv::rectangle(mainView, cv::Point(0, 0), cv::Point(s0W, 18),
                      cv::Scalar(40, 40, 80), cv::FILLED);
        cv::rectangle(mainView, cv::Point(s0W, 0), cv::Point(s0W + s1W, 18),
                      cv::Scalar(40, 80, 40), cv::FILLED);
        cv::putText(mainView,
                    "CAM " + std::to_string(slot0) + " - " + allCams[slot0]->Name(),
                    cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(200, 200, 255), 1, cv::LINE_AA);
        cv::putText(mainView,
                    "CAM " + std::to_string(slot1) + " - " + allCams[slot1]->Name(),
                    cv::Point(s0W + 6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(200, 255, 200), 1, cv::LINE_AA);

        cv::line(mainView, cv::Point(s0W, 0), cv::Point(s0W, mainH),
                 cv::Scalar(150, 150, 150), 2);

        if (showConfigSaved)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - configSavedTime).count();
            if (ms > 2000) showConfigSaved = false;
        }

        drawMainOSD(mainView, continuousSend,
                    (int)hom0.selectedPoints.size(), hom0.ready,
                    (int)hom1.selectedPoints.size(), hom1.ready,
                    showConfigSaved, s0W, numCams, slot0, slot1);

        // ── FPS 표시 (우상단) ──
        loopCount++;
        {
            auto nowL  = std::chrono::steady_clock::now();
            auto msL   = std::chrono::duration_cast<std::chrono::milliseconds>(
                             nowL - loopTimer).count();
            if (msL >= 500)
            {
                loopFps   = loopCount * 1000.0f / msL;
                loopCount = 0;
                loopTimer = nowL;
            }
        }
        float txFps = atomTxFps.load();

        char fpsBuf[48];
        snprintf(fpsBuf, sizeof(fpsBuf), "LOOP %3.0f fps", loopFps);
        putShadow(mainView, fpsBuf, cv::Scalar(180, 220, 255),
                  cv::Point(mainView.cols - 148, 22));
        snprintf(fpsBuf, sizeof(fpsBuf), " UDP %3.0f fps", txFps);
        putShadow(mainView, fpsBuf,
                  continuousSend ? cv::Scalar(60, 255, 60) : cv::Scalar(120, 120, 120),
                  cv::Point(mainView.cols - 148, 40));

        cv::imshow(mainWin, mainView);

        // ── 워프 이어붙임 창 ──
        if (hom0.ready && hom1.ready && !gray0.empty() && !gray1.empty())
        {
            cv::Mat warped0, warped1;
            cv::warpPerspective(gray0, warped0, hom0.matrix,
                                cv::Size(settings.targetWidth, settings.targetHeight));
            cv::warpPerspective(gray1, warped1, hom1.matrix,
                                cv::Size(settings.targetWidth, settings.targetHeight));

            cv::Mat wc0, wc1;
            cv::cvtColor(warped0, wc0, cv::COLOR_GRAY2BGR);
            cv::cvtColor(warped1, wc1, cv::COLOR_GRAY2BGR);

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
                int dispX = (int)c.x + settings.targetWidth;
                cv::putText(wc1,
                            "(" + std::to_string(dispX) + "," + std::to_string((int)c.y) + ")",
                            cv::Point((int)c.x + 8, (int)c.y - 5),
                            cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
            }

            cv::rectangle(wc0, cv::Point(0,0), cv::Point(wc0.cols, 18),
                          cv::Scalar(40, 40, 80), cv::FILLED);
            cv::rectangle(wc1, cv::Point(0,0), cv::Point(wc1.cols, 18),
                          cv::Scalar(40, 80, 40), cv::FILLED);
            cv::putText(wc0,
                        "CAM " + std::to_string(slot0) + " - " + allCams[slot0]->Name() + " [Warped]",
                        cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(200, 200, 255), 1, cv::LINE_AA);
            cv::putText(wc1,
                        "CAM " + std::to_string(slot1) + " - " + allCams[slot1]->Name() + " [Warped]",
                        cv::Point(6, 14), cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(200, 255, 200), 1, cv::LINE_AA);

            cv::Mat stitched;
            cv::hconcat(wc0, wc1, stitched);
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
            if (!stitchedCloseDisabled)
            {
                disableCloseButton(stitchedWin);
                stitchedCloseDisabled = true;
            }
        }

        // ── 키 입력 ──
        int key = cv::waitKey(1);

        // waitKey 직후: homStates → txHomStates 동기화
        {
            std::lock_guard<std::mutex> lk(homSyncMutex);
            txHomStates = homStates;
        }

        if (key == 113 || key == 81 || key == 27)   // q Q ESC
        {
            break;
        }
        else if (key == 119 || key == 87)            // w W — swap slots
        {
            std::swap(slot0, slot1);
            atomSlot0.store(slot0);
            atomSlot1.store(slot1);
            updateMouseData();
            std::cout << "[Swap] Slot0=Cam" << slot0
                      << "  Slot1=Cam" << slot1 << std::endl;
        }
        else if (key == '[' || key == '{')           // [ — cycle left slot
        {
            if (numCams > 2)
            {
                int next = (slot0 + 1) % numCams;
                while (next == slot1) next = (next + 1) % numCams;
                slot0 = next;
                atomSlot0.store(slot0);
                updateMouseData();
                std::cout << "[Cycle] Slot0=Cam" << slot0 << std::endl;
            }
        }
        else if (key == ']' || key == '}')           // ] — cycle right slot
        {
            if (numCams > 2)
            {
                int next = (slot1 + 1) % numCams;
                while (next == slot0) next = (next + 1) % numCams;
                slot1 = next;
                atomSlot1.store(slot1);
                updateMouseData();
                std::cout << "[Cycle] Slot1=Cam" << slot1 << std::endl;
            }
        }
        else if (key == 114 || key == 82)            // r R — reset corners
        {
            homStates[slot0].reset();
            homStates[slot1].reset();
            std::cout << "Corners reset: Cam" << slot0 << " Cam" << slot1 << std::endl;
        }
        else if (key == 117 || key == 85)            // u U — UDP toggle
        {
            continuousSend = !continuousSend;
            atomSend.store(continuousSend);
            std::cout << "[UDP] " << (continuousSend ? "ON" : "OFF") << std::endl;
        }
        else if (key == 115 || key == 83)            // s S — save all
        {
            bool ok = true;
            for (int i = 0; i < numCams; i++)
                ok = ok && saveConfig(settings, homStates[i].selectedPoints, i);
            if (ok)
            {
                showConfigSaved = true;
                configSavedTime = std::chrono::steady_clock::now();
                std::cout << "[Save] Config saved for all " << numCams << " cameras." << std::endl;
            }
        }
        else if (key == 112 || key == 80)            // p P — settings dialog
        {
            AppSettings prev = settings;
            if (ShowSettingsDialog(settings))
            {
                if (settings.exposure != prev.exposure)
                {
                    for (int i = 0; i < numCams; i++)
                        allCams[i]->SetExposure(settings.exposure);
                    std::cout << "[Settings] Exposure -> " << settings.exposure << std::endl;
                }
                if (strcmp(settings.ipAddress, prev.ipAddress) != 0 ||
                    settings.port != prev.port)
                {
                    std::lock_guard<std::mutex> lk(senderMutex);
                    sender.updateTarget(settings.ipAddress, settings.port);
                }
                if (settings.udpFps != prev.udpFps)
                {
                    atomUdpFps.store(settings.udpFps);
                    std::cout << "[Settings] UDP TX Rate -> " << settings.udpFps << " fps" << std::endl;
                }
                if (settings.targetWidth  != prev.targetWidth ||
                    settings.targetHeight != prev.targetHeight)
                {
                    updateMouseData();
                    for (auto& hs : homStates) hs.reset();
                    std::cout << "[Settings] Target res changed -> all corners reset." << std::endl;
                }
            }
        }
    }

    // ========== 정리 ==========
    atomRunning.store(false);
    txThread.join();

    cv::destroyAllWindows();
    WSACleanup();
    CameraManager::X().Shutdown();
    std::cout << "Program terminated." << std::endl;
    restoreLog();
    return 0;
}
