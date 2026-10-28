# DualSense Deadzone (Android)

DualSense 스틱/트리거 데드존을 **시스템 전체**에 적용하는 안드로이드 앱입니다.
루팅 없이 [Shizuku](https://shizuku.rikka.app/)의 셸 권한으로 동작하고, 게임에는 **진짜 DualSense(054C:0CE6)** 로 보이는 가상 패드를 만듭니다.

## 동작 원리

```
실제 DualSense
  ├─ /dev/input/eventX  게임패드   ┐
  ├─ /dev/input/eventY  모션 센서   ├─ EVIOCGRAB (게임에는 안 보이게) 후 읽기
  └─ /dev/input/eventZ  터치패드   ┘
              │
              ▼
  [DeadzoneUserService: Shizuku 셸 프로세스 + libdsdz.so]
     스틱/트리거에 데드존 적용 → DualSense USB 입력 리포트(0x01) 합성
              │
              ▼
  /dev/uhid  → 가상 DualSense (BUS_USB, 054C:0CE6)
              │
              ▼
  커널 hid-playstation 드라이버가 실제 DualSense로 인식 → 안드로이드 → 게임
```

- 커널 드라이버가 연결 시 요청하는 Feature 리포트(캘리브레이션 0x05, MAC 0x09, 펌웨어 0x20)는 데몬이 직접 만들어 응답합니다. 캘리브레이션은 모션 센서 값이 그대로 통과되도록 설계했습니다 (`ds_report.c`).
- 이 기기(Lenovo TB-J716F)는 셸이 `/dev/hidraw*`를 열 수 없어서, 원본 리포트 프록시 대신 evdev → 리포트 합성 방식을 씁니다.
- 같은 기기에서 SELinux가 셸의 `/dev/input/event*` **쓰기**를 막기 때문에 evdev는 읽기 전용으로 엽니다 (grab은 가능).
- 가상 패드는 uniq(MAC) `01:20:ad:de:d5:02`로 구분합니다. 드라이버가 phys를 비워 두기 때문입니다.

## 지원 / 미지원

| 기능 | 상태 |
|---|---|
| 스틱, 버튼, D-pad, L2/R2 아날로그 | ✅ |
| 자이로/가속도, 터치패드 | ✅ |
| 진동, 라이트바, 플레이어 LED, 적응형 트리거, 햅틱 | ❌ evdev 쓰기/hidraw가 막혀 있고, grab 상태에서는 다른 경로의 진동 명령도 커널이 무시함 |
| 배터리 잔량 | ⚠️ 가상 패드는 고정값(95%) 표시 (TODO) |
| 컨트롤러 여러 개 | ⚠️ 첫 번째 DualSense만 (TODO) |

## 프로젝트 구조

| 경로 | 역할 |
|---|---|
| `app/src/main/cpp/bridge.c` | 장치 탐색, grab, uhid 생성, 이벤트 루프, 진동 전달 |
| `app/src/main/cpp/ds_report.c` | HID 리포트 디스크립터, 입력 리포트 합성, Feature 리포트 응답 |
| `app/src/main/cpp/deadzone.c` | 원형 데드존, 안티/바깥 데드존, 응답 커브, 트리거 데드존 |
| `app/src/main/cpp/jni.c` | `NativeBridge` JNI 바인딩 |
| `app/src/main/aidl/.../IDeadzoneService.aidl` | 앱 ↔ Shizuku 서비스 인터페이스 |
| `DeadzoneUserService.kt` | Shizuku 셸 프로세스에서 도는 서비스 (네이티브 라이브러리 로드) |
| `ShizukuController.kt` | Shizuku 권한 요청, 서비스 바인딩 |
| `MainActivity.kt` | Compose UI: 스틱 시각화, 슬라이더, 시작/정지 |
| `DeadzoneConfig.kt` | 설정 모델과 저장 |

## 빌드

필요: JDK 17, Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1).
Windows에서는 SDK의 CMake가 실행되려면 Visual C++ 재배포 패키지(2015+ x64)가 있어야 합니다.

```bash
gradlew.bat assembleDebug
```

APK: `app/build/outputs/apk/debug/app-debug.apk`

## 설치와 실행

1. 폰에 [Shizuku](https://github.com/RikkaApps/Shizuku/releases)를 설치하고 무선 디버깅 또는 ADB로 시작합니다.
2. 앱 설치:
   ```bash
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   ```
3. DualSense를 블루투스로 연결합니다.
4. 앱에서 **연결** → Shizuku 권한 허용 → **시작**.
5. **시작 후 패드를 한 번 껐다 켭니다** (PS 버튼 10초 길게 → 다시 PS 버튼).
   패드가 꺼지는 순간 가상 패드를 다시 만들어 안드로이드 **컨트롤러 1번**을 차지하게 합니다.
   데드셀처럼 1번 컨트롤러만 쓰는 게임은 이 단계가 없으면 조용한 원본 패드를 잡아서 입력이 안 됩니다.
   패드가 꺼져 있는 동안에도 브리지는 계속 돌고, 다시 켜지면 자동으로 다시 잡습니다.
6. 동작 로그:
   ```bash
   adb logcat -s dsdz DeadzoneUserService ShizukuController
   ```
7. 가상 패드 확인 (`id`가 `01:20:ad:de:d5:02`인 DualSense가 새로 보여야 함):
   ```bash
   adb shell getevent -i
   ```

## 알려진 위험 / 확인할 것

- 셸 SELinux 정책이 `EVIOCGRAB`, `EVIOCSFF` ioctl을 막는 기기가 있을 수 있습니다. 시작 시 오류 메시지로 표시됩니다.
- 이 기기 커널의 hid-playstation 버전이 다르면 Feature 리포트 요청이 달라질 수 있습니다. logcat의 `unknown feature report` 경고를 확인하세요.
- 원본 패드는 grab해도 장치 목록에 남아 있어, 게임이 DualSense를 2개로 봅니다. 위 5번(패드 껐다 켜기)으로 가상 패드를 1번으로 만들어 해결합니다.
- 재부팅하면 Shizuku를 다시 시작해야 합니다.
