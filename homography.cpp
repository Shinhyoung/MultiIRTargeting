#include "homography.h"
#include <iostream>

void onMouse(int event, int x, int y, int flags, void* userdata)
{
    if (event != cv::EVENT_LBUTTONDOWN) return;

    MouseCallbackData* md = static_cast<MouseCallbackData*>(userdata);
    HomographyState*   hs = md->state;

    int imgW = md->frameWidth * 2;
    int imgH = md->frameHeight;

    // WINDOW_AUTOSIZE: OpenCV mouse callback은 이미 이미지 좌표를 반환함 (1:1).
    // GetClientRect + letterbox 변환은 WINDOW_NORMAL(리사이징 가능 창)용이며,
    // WINDOW_AUTOSIZE에서 사용하면 저해상도 화면/DPI 스케일링 환경에서
    // GetClientRect가 클리핑된 크기를 반환해 좌표가 잘못 변환되는 버그가 발생.
    if (x < 0 || y < 0 || x >= imgW || y >= imgH) return;

    float imgX = static_cast<float>(x);
    float imgY = static_cast<float>(y);

    // 좌측 이미지(원본)에서만 점 선택
    if (imgX < md->frameWidth &&
        static_cast<int>(hs->selectedPoints.size()) < HomographyState::REQUIRED_POINTS)
    {
        hs->selectedPoints.push_back(cv::Point2f(imgX, imgY));
        std::cout << "Point " << hs->selectedPoints.size() << " selected (actual): ("
                  << static_cast<int>(imgX) << ", " << static_cast<int>(imgY) << ")" << std::endl;

        if (static_cast<int>(hs->selectedPoints.size()) == HomographyState::REQUIRED_POINTS)
        {
            std::cout << "All 4 points selected. Calculating homography..." << std::endl;

            float tw = static_cast<float>(md->targetWidth  - 1);
            float th = static_cast<float>(md->targetHeight - 1);
            std::vector<cv::Point2f> dstPoints = {
                {0.f, 0.f}, {tw, 0.f}, {tw, th}, {0.f, th}
            };
            hs->matrix = cv::getPerspectiveTransform(hs->selectedPoints, dstPoints);
            hs->ready  = true;

            std::cout << "Homography matrix calculated. Warped view ready." << std::endl;
        }
    }
}

// ─────────────────────────────────────────────────────────
//  복합 창 (2카메라 나란히) 마우스 콜백
// ─────────────────────────────────────────────────────────
void onMouseCombined(int event, int x, int y, int flags, void* userdata)
{
    if (event != cv::EVENT_LBUTTONDOWN) return;

    CombinedMouseCallbackData* md = static_cast<CombinedMouseCallbackData*>(userdata);

    // 클릭 위치가 cam0 영역인지 cam1 영역인지 판별
    bool isCam1 = (x >= md->cam0Width);
    HomographyState* hs = isCam1 ? md->hom1 : md->hom0;

    // 해당 카메라 영역 내 상대 좌표
    float imgX = static_cast<float>(isCam1 ? x - md->cam0Width : x);
    float imgY = static_cast<float>(y);

    int camW = isCam1 ? md->cam1Width : md->cam0Width;
    if (imgX < 0 || imgX >= camW || imgY < 0 || imgY >= md->frameHeight) return;

    if (static_cast<int>(hs->selectedPoints.size()) >= HomographyState::REQUIRED_POINTS) return;

    hs->selectedPoints.push_back(cv::Point2f(imgX, imgY));
    std::cout << (isCam1 ? "Cam1" : "Cam0") << " point "
              << hs->selectedPoints.size() << " selected: ("
              << static_cast<int>(imgX) << ", " << static_cast<int>(imgY) << ")" << std::endl;

    if (static_cast<int>(hs->selectedPoints.size()) == HomographyState::REQUIRED_POINTS)
    {
        float tw = static_cast<float>(md->targetWidth  - 1);
        float th = static_cast<float>(md->targetHeight - 1);
        std::vector<cv::Point2f> dstPoints = {
            {0.f, 0.f}, {tw, 0.f}, {tw, th}, {0.f, th}
        };
        hs->matrix = cv::getPerspectiveTransform(hs->selectedPoints, dstPoints);
        hs->ready  = true;
        std::cout << (isCam1 ? "Cam1" : "Cam0")
                  << " homography calculated. Warped view ready." << std::endl;
    }
}
