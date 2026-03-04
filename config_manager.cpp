#include "config_manager.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

// ─────────────────────────────────────────────────────────

std::string getExeDir()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos + 1) : ".\\";
}

// camIdx에 따른 설정 파일 경로 반환
static std::string getConfigFilePath(int camIdx)
{
    std::string confDir = getExeDir() + "conf";
    if (camIdx == 0)
        return confDir + "\\setting.cfg";
    else
        return confDir + "\\setting_cam" + std::to_string(camIdx) + ".cfg";
}

// ─────────────────────────────────────────────────────────

bool saveConfig(const AppSettings& settings, const std::vector<cv::Point2f>& corners, int camIdx)
{
    // conf/ 폴더 생성 (이미 있으면 무시)
    std::string confDir = getExeDir() + "conf";
    if (!CreateDirectoryA(confDir.c_str(), nullptr))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            std::cerr << "[Config] Cannot create directory: " << confDir
                      << "  (Error " << GetLastError() << ")" << std::endl;
            return false;
        }
    }

    std::string filePath = getConfigFilePath(camIdx);
    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "[Config] Cannot write: " << filePath << std::endl;
        return false;
    }

    // 전역 설정은 카메라 0 파일에만 저장
    if (camIdx == 0)
    {
        f << "ip="            << settings.ipAddress   << "\n";
        f << "port="          << settings.port         << "\n";
        f << "target_width="  << settings.targetWidth  << "\n";
        f << "target_height=" << settings.targetHeight << "\n";
        f << "exposure="      << settings.exposure     << "\n";
    }

    f << "corner_count="  << corners.size()        << "\n";
    for (size_t i = 0; i < corners.size(); i++)
    {
        f << "corner" << i << "_x=" << static_cast<int>(corners[i].x) << "\n";
        f << "corner" << i << "_y=" << static_cast<int>(corners[i].y) << "\n";
    }

    std::cout << "[Config] Saved to: " << filePath << std::endl;
    return true;
}

// ─────────────────────────────────────────────────────────

bool loadConfig(AppSettings& settings, std::vector<cv::Point2f>& corners, int camIdx)
{
    std::string filePath = getConfigFilePath(camIdx);
    std::ifstream f(filePath);
    if (!f.is_open()) return false;

    corners.clear();
    int   cornerCount = 0;
    float cx[4] = {}, cy[4] = {};

    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (val.empty()) continue;

        try
        {
            if      (key == "ip")            { strncpy_s(settings.ipAddress, sizeof(settings.ipAddress), val.c_str(), _TRUNCATE); }
            else if (key == "port")          { int p = std::stoi(val); if (p > 0 && p <= 65535) settings.port = p; }
            else if (key == "target_width")  { int w = std::stoi(val); if (w > 0) settings.targetWidth  = w; }
            else if (key == "target_height") { int h = std::stoi(val); if (h > 0) settings.targetHeight = h; }
            else if (key == "exposure")      { int e = std::stoi(val); settings.exposure = std::max(0, std::min(7500, e)); }
            else if (key == "corner_count")  { cornerCount = std::stoi(val); }
            else if (key.size() > 7 && key.substr(0, 6) == "corner")
            {
                size_t under = key.find('_', 6);
                if (under != std::string::npos)
                {
                    int         idx  = std::stoi(key.substr(6, under - 6));
                    std::string axis = key.substr(under + 1);
                    if (idx >= 0 && idx < 4)
                    {
                        if      (axis == "x") cx[idx] = std::stof(val);
                        else if (axis == "y") cy[idx] = std::stof(val);
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            std::cerr << "[Config] Parse error on line: " << line << std::endl;
        }
    }

    int n = std::min(cornerCount, 4);
    for (int i = 0; i < n; i++)
        corners.emplace_back(cx[i], cy[i]);

    std::cout << "[Config] Cam" << camIdx << " loaded: " << filePath
              << "  Corners=" << corners.size() << std::endl;
    return true;
}
