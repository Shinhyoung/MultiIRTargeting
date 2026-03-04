# OptiTrack Flex 13 IR Tracking System

OptiTrack Flex 13 카메라를 사용한 실시간 적외선 영상 추적 및 호모그래피 변환 시스템

## 목차

1. [프로젝트 개요](#프로젝트-개요)
2. [주요 기능](#주요-기능)
3. [시스템 요구사항](#시스템-요구사항)
4. [설치 가이드](#설치-가이드)
5. [빌드 방법](#빌드-방법)
6. [사용 방법](#사용-방법)
7. [UDP 통신](#udp-통신)
8. [문제 해결](#문제-해결)

---

## 프로젝트 개요

OptiTrack Flex 13 카메라의 IR 영상을 실시간으로 처리하고, 관심 영역을 선택하여 호모그래피 변환을 적용한 뒤 UDP로 좌표를 전송하는 시스템입니다.

### 개발 환경
- **OS**: Windows 10/11
- **언어**: C++17
- **빌드 시스템**: CMake
- **컴파일러**: Visual Studio 2022 (MSVC)

---

## 주요 기능

### 영상 처리
- Grayscale IR 영상 실시간 캡처
- Morphological Dilation (3회, 3×3 커널)
- Binary Threshold (임계값 200)
- 객체 중심점 자동 검출 및 좌표 표시

### 호모그래피 변환
- 마우스 클릭으로 관심 영역 선택 (4개 점)
- 설정된 타깃 해상도(기본 1024×768)로 원근 변환
- 4점 영역 내 좌표만 검출 및 표시

### 설정 저장 / 자동 복원
- **S** 키로 현재 설정 + 4개 코너 포인트를 `conf/setting.cfg`에 저장
- 다음 실행 시 설정 파일이 존재하면 자동으로 불러와 호모그래피 복원 + UDP 스트리밍 자동 시작
- 설정 파일이 없을 경우에만 시작 시 설정 다이얼로그 표시
- **P** 키로 런타임 중 설정 재변경 가능, 변경 즉시 적용 (노출, UDP 주소, 해상도)

| 설정 항목 | 기본값 | 설명 |
|-----------|--------|------|
| IP Address | 127.0.0.1 | UDP 전송 대상 IP |
| Port | 7777 | UDP 전송 포트 |
| Target Width | 1024 | 호모그래피 출력 너비 (px) |
| Target Height | 768 | 호모그래피 출력 높이 (px) |
| Exposure | 7500 | 카메라 노출 (0~7500) |

### UDP 좌표 전송
- 실시간 연속 전송 모드 (**U** 키 토글)
- 호모그래피 설정 완료 후에만 전송
- 4점 영역 내에 있는 포인트 좌표만 전송
- 패킷 포맷: `x1,y1;x2,y2;...`

---

## 시스템 요구사항

### 하드웨어
- **카메라**: OptiTrack Flex 13 (USB 연결)
- **메모리**: 최소 4GB RAM

### 소프트웨어

| 구분 | 소프트웨어 | 버전 | 설치 위치 |
|------|-----------|------|-----------|
| SDK | OptiTrack Camera SDK | 3.4.0 | `C:\Program Files (x86)\OptiTrack\CameraSDK` |
| 라이브러리 | OpenCV | 4.5.4 | `C:\opencv\opencv\build` |
| 빌드 도구 | CMake | 3.15 이상 | - |
| 컴파일러 | Visual Studio Build Tools | 2022 | - |

---

## 설치 가이드

### 1. OptiTrack Camera SDK 설치

1. [OptiTrack Developer Tools](https://optitrack.com/support/downloads/developer-tools.html) 에서 Camera SDK 3.4.0 다운로드
2. 설치 경로: `C:\Program Files (x86)\OptiTrack\CameraSDK` (기본값 유지 권장)
3. 설치 확인:
   ```
   C:\Program Files (x86)\OptiTrack\CameraSDK\
   ├── include\cameralibrary.h
   └── lib\CameraLibrary2019x64S.lib
       CameraLibrary2019x64S.dll
   ```

### 2. OpenCV 설치

1. [OpenCV 4.5.4 Releases](https://github.com/opencv/opencv/releases/tag/4.5.4) 에서 `opencv-4.5.4-vc14_vc15.exe` 다운로드
2. 압축 해제 경로: `C:\opencv` (권장)
3. 설치 확인:
   ```
   C:\opencv\opencv\build\
   ├── include\opencv2\
   └── x64\vc15\
       ├── bin\opencv_world454.dll
       └── lib\opencv_world454.lib
   ```

### 3. CMake 및 Visual Studio Build Tools 설치

- [CMake 다운로드](https://cmake.org/download/) — 설치 시 **"Add CMake to system PATH"** 체크
- [VS Build Tools 2022 다운로드](https://visualstudio.microsoft.com/downloads/) — **"C++를 사용한 데스크톱 개발"** 워크로드 선택

---

## 빌드 방법

```bash
# 1. 빌드 디렉토리 생성
mkdir build && cd build

# 2. CMake 설정
cmake .. -G "Visual Studio 17 2022" -A x64

# 3. Release 빌드
cmake --build . --config Release
```

빌드 완료 후 생성 파일:
```
build\Release\
├── IRViewer.exe
├── UDPReceiver.exe
├── CameraLibrary2019x64S.dll
└── opencv_world454.dll
```

---

## 사용 방법

### 1. IRViewer 실행

```bash
build\Release\IRViewer.exe
```

실행 시 **설정 창**이 자동으로 표시됩니다. 값 확인 후 **OK**를 누르면 카메라가 시작됩니다.

### 2. 화면 구성

| 영역 | 내용 |
|------|------|
| 왼쪽 | Grayscale 원본 IR 영상 + 선택한 코너 포인트 |
| 오른쪽 (호모그래피 전) | Binary Threshold 영상 + 검출 좌표 |
| 오른쪽 (호모그래피 후) | 타깃 해상도로 변환된 Warped 영상 + 변환 좌표 |
| 오른쪽 상단 좌측 | 설정된 타깃 해상도 텍스트 표시 (예: `1920 x 1080`) |

### 3. 호모그래피 설정 순서

1. 왼쪽 영상에서 **마우스 좌클릭**으로 4개 점 선택 (순서 중요)
   1. 왼쪽 위 (Left Top)
   2. 오른쪽 위 (Right Top)
   3. 오른쪽 아래 (Right Bottom)
   4. 왼쪽 아래 (Left Bottom)
2. 4점 선택 완료 시 오른쪽에 Warped View 자동 전환
3. 이후 영역 내 좌표만 검출 및 UDP 전송 대상이 됨

### 4. 단축키

| 키 | 기능 |
|----|------|
| **마우스 좌클릭** | 왼쪽 영상에서 코너 포인트 선택 |
| **U** | UDP 실시간 전송 ON/OFF 토글 |
| **S** | 현재 설정 + 코너 포인트 저장 (`conf/setting.cfg`) |
| **R** | 선택한 코너 포인트 초기화 |
| **P** | 설정 창 열기 (런타임 변경 즉시 적용) |
| **Q** / **ESC** | 프로그램 종료 |

### 5. 런타임 설정 변경 (P 키)

P 키로 설정 창을 열면:
- **Exposure** 변경 → 카메라에 즉시 적용
- **IP / Port** 변경 → UDP 전송 주소 즉시 갱신
- **Target Width/Height** 변경 → 호모그래피 리셋 후 재설정 필요

---

## UDP 통신

### IRViewer → 수신 프로그램

| 항목 | 내용 |
|------|------|
| 프로토콜 | UDP |
| 기본 포트 | 7777 |
| 패킷 포맷 | `x1,y1;x2,y2;...` |
| 좌표 범위 | 0 ~ (targetWidth-1), 0 ~ (targetHeight-1) |
| 전송 조건 | 호모그래피 설정 완료 + U 키 ON + 영역 내 포인트 존재 |

**패킷 예시** (포인트 2개):
```
312,456;789,123
```

### UDPReceiver 실행 (테스트용)

```bash
build\Release\UDPReceiver.exe
```

실행 시 캔버스 해상도 설정 창이 표시됩니다 (기본값: 1024×768).
IRViewer에서 **U 키**로 전송을 시작하면 실시간으로 좌표를 시각화합니다.

### Unreal Engine 연동

`IRTargetingPlugin` (별도 프로젝트)을 통해 UE5에서 수신 가능합니다:
- `UIRTargetingSubsystem` — GameInstance 서브시스템 (자동 초기화)
- `OnPointsReceived` / `OnFirstPointReceived` — Blueprint 델리게이트
- 별도 스레드 비동기 수신으로 게임 루프 영향 없음

---

## 프로젝트 구조

```
IRTargeting2/
├── main.cpp              # 진입점: 카메라 초기화 + 메인 루프 (~210줄)
├── settings.h/.cpp       # AppSettings 구조체 + Win32 설정 다이얼로그
├── homography.h/.cpp     # HomographyState 구조체 + 마우스 콜백 (onMouse)
├── udp_sender.h/.cpp     # UDPSender 클래스 (소켓 생성/전송/소멸)
├── frame_processor.h/.cpp# 영상 처리 파이프라인 (Dilate→Threshold→Contour→Warp)
├── osd_renderer.h/.cpp   # OSD 렌더링 (단축키 안내 + 상태 표시)
├── config_manager.h/.cpp # 설정 저장/불러오기 (conf/setting.cfg)
├── udp_receiver.cpp      # UDP 수신 테스트 프로그램 (독립 실행)
├── CMakeLists.txt        # CMake 빌드 설정
├── README.md             # 이 문서
├── CLAUDE.md             # 프로젝트 요구사항
├── .gitignore
├── conf/                 # 설정 파일 디렉토리 (S 키로 자동 생성)
│   └── setting.cfg       # 저장된 설정 + 코너 포인트
└── build/                # 빌드 출력 (생성됨)
    └── Release/
        ├── IRViewer.exe
        └── UDPReceiver.exe
```

---

## 문제 해결

### 카메라가 인식되지 않음
- USB 연결 상태 확인
- Motive 등 다른 OptiTrack 프로그램 종료 (동시 사용 불가)
- `IRViewer_log.txt` 로그 파일 확인

### DLL 오류
- `build\Release\` 폴더에 `CameraLibrary2019x64S.dll`, `opencv_world454.dll` 존재 여부 확인
- CMake 빌드 시 자동 복사됨 — 빌드를 다시 실행

### CMake 경로 오류
`CMakeLists.txt`에서 경로 확인:
```cmake
set(CAMERA_SDK_PATH "C:/Program Files (x86)/OptiTrack/CameraSDK")
set(OPENCV_PATH     "C:/opencv/opencv/build")
```

### UDP 좌표가 전송되지 않음
1. **U 키**로 전송 ON 상태 확인 (OSD 좌상단 표시)
2. 왼쪽 영상에서 **4개 코너 포인트** 선택 완료 여부 확인
3. **P 키** → 설정 창에서 IP/Port 확인

### 4점 선택 시 포인트가 클릭한 위치와 다른 곳에 표시됨
저해상도 노트북 또는 Windows DPI 스케일링(125%, 150%)이 설정된 환경에서 발생.
v2.1.1에서 수정됨 — `WINDOW_AUTOSIZE` 모드에서 `GetClientRect` 기반 letterbox 변환 제거,
OpenCV mouse callback의 좌표를 이미지 좌표로 직접 사용하도록 변경.

---

## 기술 스택

| 구분 | 내용 |
|------|------|
| 카메라 SDK | OptiTrack Camera SDK 3.4.0 |
| 영상 처리 | OpenCV 4.5.4 |
| 네트워크 | Winsock2 (UDP) |
| UI | Win32 API (설정 다이얼로그) |
| 알고리즘 | Morphological Dilation, Binary Threshold, Contour Detection, Image Moments, Homography |

---

**마지막 업데이트**: 2026-03-04
**버전**: 2.2.0 (설정 저장/자동 복원, 오른쪽 패널 해상도 표시, 단축키 변경)
