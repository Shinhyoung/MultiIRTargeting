#pragma once

#include "settings.h"
#include <opencv2/core/types.hpp>
#include <vector>
#include <string>

// 실행 파일이 위치한 디렉토리 반환 (끝에 '\' 포함)
std::string getExeDir();

// 모든 설정값과 코너 포인트를 <exeDir>/conf/setting[_camN].cfg 에 저장
// camIdx=0 → setting.cfg (하위 호환), camIdx>0 → setting_camN.cfg
// conf/ 폴더가 없으면 자동 생성. 성공 시 true 반환.
bool saveConfig(const AppSettings& settings, const std::vector<cv::Point2f>& corners, int camIdx = 0);

// <exeDir>/conf/setting[_camN].cfg 에서 설정값과 코너 포인트를 불러옴
// camIdx=0 → setting.cfg (하위 호환), camIdx>0 → setting_camN.cfg
// 파일이 없거나 파싱 오류 시 false 반환.
bool loadConfig(AppSettings& settings, std::vector<cv::Point2f>& corners, int camIdx = 0);
