#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

// ========== 호모그래피 상태 ==========
struct HomographyState
{
    static constexpr int REQUIRED_POINTS = 4;

    std::vector<cv::Point2f> selectedPoints;
    cv::Mat                  matrix;
    bool                     ready = false;

    void reset()
    {
        selectedPoints.clear();
        ready = false;
    }
};

// ========== 마우스 콜백용 데이터 구조체 ==========
struct MouseCallbackData
{
    std::string      windowName;
    int              frameWidth;
    int              frameHeight;
    int              targetWidth;
    int              targetHeight;
    HomographyState* state;    // 전역 대신 포인터로 주입
};

void onMouse(int event, int x, int y, int flags, void* userdata);

// ========== 복합 창 마우스 콜백 (2카메라 나란히 배치) ==========
// 메인 창 레이아웃: [cam0 영역(0..cam0Width-1)] | [cam1 영역(cam0Width..)]
struct CombinedMouseCallbackData
{
    int cam0Width;      // cam0 영역 너비 (클릭 x < cam0Width → cam0 선택)
    int cam1Width;      // cam1 영역 너비
    int frameHeight;    // 창 높이
    int targetWidth;
    int targetHeight;
    HomographyState* hom0;
    HomographyState* hom1;
};

void onMouseCombined(int event, int x, int y, int flags, void* userdata);
