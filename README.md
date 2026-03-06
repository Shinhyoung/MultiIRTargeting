# OptiTrack Flex 13 Multi-Camera IR Tracking System

OptiTrack Flex 13 **다중 카메라**를 사용한 실시간 적외선 영상 추적 및 호모그래피 변환 시스템

## 목차

1. [프로젝트 개요](#프로젝트-개요)
2. [주요 기능](#주요-기능)
3. [시스템 요구사항](#시스템-요구사항)
4. [빌드 방법](#빌드-방법)
5. [사용 방법](#사용-방법)
6. [좌표 체계](#좌표-체계)
7. [UDP 통신](#udp-통신)
8. [설정 파일](#설정-파일)
9. [문제 해결](#문제-해결)

---

## 프로젝트 개요

OptiTrack Flex 13 카메라 **2대 이상을 동시에** 운용하여 IR 영상을 실시간으로 처리합니다.
두 카메라의 영상을 하나의 창에 나란히 표시하고, 각 카메라에서 독립적으로 호모그래피를 설정한 뒤
두 영상을 이어 붙인 스티칭 뷰를 제공합니다. 검출된 좌표는 UDP로 외부 시스템에 전송됩니다.

**TX 스레드 분리 아키텍처** — 캡처·처리·UDP 전송이 별도 스레드에서 동작하여 화면 렌더링과 독립적으로 최대 전송 속도를 보장합니다.

### 개발 환경
- **OS**: Windows 10/11
- **언어**: C++17
- **빌드 시스템**: CMake
- **컴파일러**: Visual Studio 2022 (MSVC)

---

## 주요 기능

### 다중 카메라 동시 처리
- 연결된 OptiTrack Flex 13 카메라 **2대 이상 동시 초기화 및 구동** (N대 범용)
- 두 카메라 영상을 하나의 창에 좌(Slot0) / 우(Slot1) 나란히 표시
- 런타임 카메라 슬롯 교체: **W**(스왑), **[** / **]**(순환, 3대 이상)
- 카메라별 독립 호모그래피 상태 관리

### 스레드 분리 아키텍처
```
TX Thread (독립 루프)                Main/Display Thread
────────────────────────────         ────────────────────────────
카메라 캡처 (LatestFrame)            프레임 스냅샷 (mutex)
processFrame (blob 검출)             mainView / stitched 렌더링
UDP 전송 (fps 제한 적용)            키 입력 처리 (waitKey)
atomTxFps 갱신                       OSD + FPS 표시
```
- **TX Thread**: 화면 렌더링에 영향 없이 카메라 프레임 레이트로 독립 동작
- **Display Thread**: `waitKey(1)` 기반 이벤트 루프
- 뮤텍스 3개로 안전한 데이터 공유: `frameMutex`, `homSyncMutex`, `senderMutex`

### 화면 구성

```
┌──────────────────────────────────────────────────────────┐
│  IR View (메인 창)                                        │
│  ┌──────────────────┬──────────────────┐                  │
│  │   CAM 0 - Flex13 │   CAM 1 - Flex13 │                  │
│  │  (IR 원본)        │  (IR 원본)        │                  │
│  │  · 코너 포인트    │  · 코너 포인트    │                  │
│  │  · 블롭 중심      │  · 블롭 중심      │                  │
│  └──────────────────┴──────────────────┘                  │
└──────────────────────────────────────────────────────────┘

두 카메라 모두 4점 선택 완료 시 자동 오픈:

┌──────────────────────────────────────────────────────────┐
│  Warped Stitched View (스티칭 창)                         │
│  ┌──────────────────┬──────────────────┐                  │
│  │ CAM 0 [Warped]   │ CAM 1 [Warped]   │                  │
│  │  타깃 해상도로   │  타깃 해상도로   │                  │
│  │  변환된 영상     │  변환된 영상     │                  │
│  │  · 빨간 점 = 검출│  · 빨간 점 = 검출│                  │
│  │  · 오프셋 좌표   │  · 오프셋 좌표   │                  │
│  └──────────────────┴──────────────────┘                  │
└──────────────────────────────────────────────────────────┘
```

### 영상 처리 파이프라인
- Grayscale IR 영상 실시간 캡처
- Morphological Dilation (3회, 3×3 커널)
- Binary Threshold (임계값 200)
- Contour 기반 객체 중심점 자동 검출

### 호모그래피 변환
- 메인 창에서 마우스 좌클릭으로 각 카메라 영역에 4개 코너 포인트 선택
  - 클릭 위치가 **왼쪽 절반** → Cam0 코너 선택
  - 클릭 위치가 **오른쪽 절반** → Cam1 코너 선택
- 두 카메라 모두 4점 완료 시 스티칭 창 자동 오픈
- 각 카메라의 영상을 타깃 해상도(기본 1024×768)로 원근 변환

### 설정 저장 / 자동 복원
- **S** 키로 전체 카메라 코너 포인트를 카메라별 파일로 저장
- 다음 실행 시 두 카메라 모두 코너가 복원되면 스티칭 창 + UDP 스트리밍 자동 시작
- 설정 파일이 없을 경우에만 초기 설정 다이얼로그 표시

| 설정 항목 | 기본값 | 설명 |
|-----------|--------|------|
| IP Address | 127.0.0.1 | UDP 전송 대상 IP |
| Port | 7777 | UDP 전송 포트 |
| Target Width | 1024 | 호모그래피 출력 너비 (px) |
| Target Height | 768 | 호모그래피 출력 높이 (px) |
| Exposure | 7500 | 카메라 노출 (0~7500, 전체 카메라 공통 적용) |
| UDP TX Rate | 60 | UDP 전송 속도 제한 (1~1000 fps) |

---

## 시스템 요구사항

### 하드웨어
- **카메라**: OptiTrack Flex 13 × **2대** (USB 연결)
- **메모리**: 최소 4GB RAM

### 소프트웨어

| 구분 | 소프트웨어 | 버전 | 설치 위치 |
|------|-----------|------|-----------|
| SDK | OptiTrack Camera SDK | 3.4.0 | `C:\Program Files (x86)\OptiTrack\CameraSDK` |
| 라이브러리 | OpenCV | 4.5.4 | `C:\opencv\opencv\build` |
| 빌드 도구 | CMake | 3.15 이상 | - |
| 컴파일러 | Visual Studio Build Tools | 2022 | - |

---

## 빌드 방법

```bash
# 1. 빌드 디렉토리 생성 후 CMake 설정
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# 2. Release 빌드
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

### 1. 실행

```bash
build\Release\IRViewer.exe
```

- 설정 파일이 없으면 초기 설정 다이얼로그가 표시됩니다. **OK** 클릭 후 진행.
- 카메라가 2대 미만이면 오류 메시지와 함께 종료됩니다.

### 2. 호모그래피 설정 순서

1. **메인 창 왼쪽 절반**에서 마우스 좌클릭으로 Cam0의 코너 4점 선택
2. **메인 창 오른쪽 절반**에서 마우스 좌클릭으로 Cam1의 코너 4점 선택
3. 각 카메라별로 4점 완료 즉시 호모그래피 계산
4. 두 카메라 모두 완료되면 **Warped Stitched View** 창이 자동으로 열림

**코너 선택 순서** (각 카메라 공통):
```
1번(좌상) ──── 2번(우상)
   │                   │
4번(좌하) ──── 3번(우하)
```

### 3. 단축키

| 키 | 기능 |
|----|------|
| **마우스 좌클릭** (메인 창 왼쪽) | Cam0 코너 포인트 선택 |
| **마우스 좌클릭** (메인 창 오른쪽) | Cam1 코너 포인트 선택 |
| **W** | 좌/우 슬롯 교체 (Slot0 ↔ Slot1) |
| **[** | 좌 슬롯 카메라 순환 (3대 이상 연결 시) |
| **]** | 우 슬롯 카메라 순환 (3대 이상 연결 시) |
| **U** | UDP 실시간 전송 ON/OFF 토글 |
| **S** | 전체 설정 + 코너 포인트 저장 |
| **R** | 전체 카메라 코너 포인트 초기화 |
| **P** | 설정 창 열기 (노출·IP·포트·해상도 변경, 즉시 적용) |
| **Q** / **ESC** | 프로그램 종료 |

### 4. OSD (화면 표시 정보)

- 좌상단 박스: 단축키 안내
- **우상단**: `LOOP xxx fps` (화면 렌더링 속도), `UDP xxx fps` (실제 전송 속도)
- 각 카메라 영역 하단: `Cam0: N/4 pts` / `Cam0: READY`
- 하단 중앙: UDP 전송 상태 (`udp SENDING` / `udp STOPPED`)

---

## 좌표 체계

### 스티칭 좌표 공간

두 카메라의 워프된 좌표는 **단일 연속 좌표 공간**으로 통합됩니다:

| 카메라 | X 좌표 범위 | Y 좌표 범위 |
|--------|------------|------------|
| Cam0 | `0` ~ `targetWidth - 1` | `0` ~ `targetHeight - 1` |
| Cam1 | `targetWidth` ~ `2 × targetWidth - 1` | `0` ~ `targetHeight - 1` |

예시 (targetWidth=1024, targetHeight=768):
- Cam0 검출 좌표: `(312, 456)` → UDP 전송: `(312, 456)`
- Cam1 검출 좌표: `(200, 300)` → UDP 전송: `(1224, 300)` *(+1024 오프셋)*

스티칭 창에 표시되는 텍스트 좌표도 동일한 오프셋이 적용됩니다.

---

## UDP 통신

### 전송 규격

| 항목 | 내용 |
|------|------|
| 프로토콜 | UDP |
| 기본 포트 | 7777 |
| 패킷 포맷 | `x1,y1;x2,y2;...` |
| 전송 조건 | 두 카메라 모두 호모그래피 완료 + **U** 키 ON + 영역 내 포인트 존재 |

**패킷 예시** (Cam0에서 1개, Cam1에서 1개 검출 시):
```
# Cam0 패킷
312,456

# Cam1 패킷 (오프셋 적용)
1224,300
```

각 카메라의 검출 좌표는 **별도 UDP 패킷**으로 전송됩니다.

### UDPReceiver 실행 (테스트용)

```bash
build\Release\UDPReceiver.exe
```

---

## 설정 파일

**저장 위치**: 실행 파일과 동일 폴더의 `conf\` 디렉토리

```
build\Release\conf\
├── setting.cfg          ← Cam0 코너 + 전역 설정 (IP, Port, 해상도, 노출)
└── setting_cam1.cfg     ← Cam1 코너만
```

### setting.cfg 포맷 (Cam0)

```ini
ip=127.0.0.1
port=7777
target_width=1024
target_height=768
exposure=7500
udp_fps=60
corner_count=4
corner0_x=120
corner0_y=85
corner1_x=380
corner1_y=90
corner2_x=375
corner2_y=310
corner3_x=115
corner3_y=315
```

### setting_cam1.cfg 포맷 (Cam1)

```ini
corner_count=4
corner0_x=45
corner0_y=92
corner1_x=455
corner1_y=88
corner2_x=460
corner2_y=308
corner3_x=40
corner3_y=312
```

---

## 프로젝트 구조

```
MultiIRTargeting/
├── main.cpp                # 진입점: 다중 카메라 초기화 + 메인 루프
├── settings.h/.cpp         # AppSettings 구조체 + Win32 설정 다이얼로그
├── homography.h/.cpp       # HomographyState + 단일/복합 마우스 콜백
├── udp_sender.h/.cpp       # UDPSender 클래스 (소켓 생성/전송/소멸)
├── frame_processor.h/.cpp  # 영상 처리 파이프라인 (Dilate→Threshold→Contour→Warp)
├── osd_renderer.h/.cpp     # OSD 렌더링 (단축키 안내 + 상태 표시)
├── config_manager.h/.cpp   # 설정 저장/불러오기 (카메라별 파일)
├── udp_receiver.cpp        # UDP 수신 테스트 프로그램 (독립 실행)
├── CMakeLists.txt          # CMake 빌드 설정
├── README.md               # 이 문서
└── build/                  # 빌드 출력 (생성됨)
    └── Release/
        ├── IRViewer.exe
        ├── UDPReceiver.exe
        └── conf/
            ├── setting.cfg
            └── setting_cam1.cfg
```

---

## 문제 해결

### 카메라가 2대 인식되지 않음
- USB 연결 상태 확인 (각각 별도 USB 포트 권장)
- Motive 등 다른 OptiTrack 프로그램 종료 (동시 사용 불가)
- `IRViewer_log.txt` 로그 파일 확인

### DLL 오류
- `build\Release\` 폴더에 `CameraLibrary2019x64S.dll`, `opencv_world454.dll` 존재 여부 확인
- CMake 빌드 시 자동 복사됨 — `cmake --build . --config Release` 재실행

### UDP 좌표가 전송되지 않음
1. **두 카메라 모두** 4개 코너 포인트 선택 완료 여부 확인 (OSD에 `READY` 표시)
2. **U 키**로 전송 ON 상태 확인
3. **P 키** → 설정 창에서 IP/Port 확인

### 4점 선택 시 클릭 위치가 틀림
Windows DPI 스케일링(125%, 150%)이 설정된 환경에서 발생 가능.
`WINDOW_AUTOSIZE` 모드를 사용하므로 OpenCV mouse callback 좌표가 이미지 좌표와 1:1 대응합니다.

---

## 기술 스택

| 구분 | 내용 |
|------|------|
| 카메라 SDK | OptiTrack Camera SDK 3.4.0 |
| 영상 처리 | OpenCV 4.5.4 |
| 네트워크 | Winsock2 (UDP) |
| UI | Win32 API (설정 다이얼로그) |
| 알고리즘 | Morphological Dilation, Binary Threshold, Contour Detection, Image Moments, Perspective Homography, Image Stitching |

---

**마지막 업데이트**: 2026-03-06
**버전**: 4.0.0

주요 변경:
- N대 카메라 범용 슬롯 시스템 (`W` 스왑, `[`/`]` 순환)
- TX/Display 스레드 분리 아키텍처 (렌더링 독립 UDP 전송)
- UDP TX Rate 설정 (1~1000 fps, 기본 60fps)
- 실시간 FPS 표시 (LOOP fps / UDP fps)
- 윈도우 X 버튼 비활성화 (Q/ESC 키로만 종료)
