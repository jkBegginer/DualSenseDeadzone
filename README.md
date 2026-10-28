# DualSense Deadzone (Android)

System-wide stick and trigger deadzone for the PlayStation 5 DualSense controller on Android.
No root needed: it runs with [Shizuku](https://shizuku.rikka.app/) (ADB shell privileges) and presents games with a **virtual DualSense (054C:0CE6)** that the kernel treats as a real one.

[한국어 설명은 아래에 있습니다 ↓](#한국어)

> **Unofficial project.** Not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
> "DualSense" and "PlayStation" are trademarks of Sony Interactive Entertainment.

## Features

- Radial stick deadzone (keeps diagonals), outer deadzone, anti-deadzone, response curve — per stick
- L2/R2 start/end points
- Fixes stick drift system-wide, in every game
- Gyro, accelerometer and touchpad keep working
- Live view of raw vs. adjusted stick position

## Compatibility

**Tested on one device only:** Lenovo Tab P11 Pro (TB-J716F), Android 13, DualSense over Bluetooth.
Other devices may behave differently (SELinux policy, kernel driver version). Reports from other devices are welcome — please open an issue with your device, Android version and `adb logcat -s dsdz` output.

| | Status |
|---|---|
| Sticks, buttons, D-pad, L2/R2 analog | ✅ |
| Gyro / accelerometer, touchpad | ✅ |
| Rumble, lightbar, player LEDs, adaptive triggers | ❌ Not forwarded (shell cannot write to the real controller) |
| Battery level | ⚠️ Virtual pad shows a fixed 95% |
| Multiple controllers | ⚠️ First DualSense only |

## Install

1. Install and start [Shizuku](https://github.com/RikkaApps/Shizuku/releases) (wireless debugging or ADB).
2. Download the APK from [Releases](../../releases) and install it.
3. Connect the DualSense over Bluetooth.
4. In the app: **Connect** → allow Shizuku permission → **Start**.
5. Play. Adjust the sliders at any time; changes apply instantly.
   Most games work right away (tested: Dave the Diver, Hollow Knight: Silksong).

**If a game gets no controller input at all** (tested: Dead Cells), turn the controller off and on once while the app is running (hold PS ~10 s, then press PS).
Some games only read Android's controller #1, which is still the real (now silent) pad. Power-cycling makes the virtual pad take controller #1.

To undo everything, tap **Stop**, force-stop the app, or reboot. Nothing is written to the system.

## How it works

```
Real DualSense
  ├─ /dev/input/eventX  gamepad   ┐
  ├─ /dev/input/eventY  motion    ├─ EVIOCGRAB (hidden from Android), read-only
  └─ /dev/input/eventZ  touchpad  ┘
              │
              ▼
  [DeadzoneUserService: Shizuku shell process + libdsdz.so]
     apply deadzones → synthesize DualSense USB input report 0x01
              │
              ▼
  /dev/uhid → virtual DualSense (BUS_USB, 054C:0CE6)
              │
              ▼
  kernel hid-playstation binds it as a real DualSense → Android → games
```

- The daemon answers the driver's feature-report requests (calibration 0x05, pairing 0x09, firmware 0x20). Calibration is chosen so motion values pass through unchanged.
- `/dev/hidraw*` is not accessible to the shell, so input is read from evdev and re-encoded instead of proxied.
- When the controller drops, the virtual pad is recreated while controller #1 is free, then the real pad is re-grabbed when it returns.

## Build

Requires JDK 17 and Android SDK (platform 35, build-tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1).
On Windows, the SDK's CMake needs the Visual C++ 2015+ x64 redistributable.

```bash
./gradlew assembleDebug
```

Release builds are signed when a `keystore.properties` file exists in the project root (not committed):

```properties
storeFile=C:/path/to/release.jks
storePassword=...
keyAlias=...
keyPassword=...
```

## License

[MIT](LICENSE). Bundles the Shizuku API library (Apache-2.0, © RikkaW).

---

## 한국어

DualSense 스틱/트리거 데드존을 **시스템 전체**에 적용하는 안드로이드 앱입니다.
루팅 없이 Shizuku의 셸 권한으로 동작하고, 게임에는 커널이 진짜로 인식하는 **가상 DualSense**를 보여 줍니다.

> Sony Interactive Entertainment와 무관한 **비공식 프로젝트**입니다.

### 기능
- 스틱별 원형 데드존(대각선 유지), 바깥 데드존, 안티 데드존, 응답 커브
- L2/R2 시작/최대 지점
- 모든 게임에서 스틱 드리프트 보정
- 자이로, 가속도, 터치패드 그대로 동작
- 원본/보정 후 스틱 위치 실시간 표시

### 호환성
**Lenovo Tab P11 Pro(TB-J716F), Android 13, 블루투스 연결에서만 테스트했습니다.**
기기마다 SELinux 정책이나 커널 드라이버가 달라서 다른 기기에서는 안 될 수 있습니다. 다른 기기에서 써 보셨다면 기기명, 안드로이드 버전, `adb logcat -s dsdz` 결과와 함께 이슈로 알려 주세요.

- ✅ 스틱, 버튼, D-pad, L2/R2 아날로그, 자이로, 터치패드
- ❌ 진동, 라이트바, LED, 적응형 트리거 (셸 권한으로는 원본 패드에 쓸 수 없음)
- ⚠️ 배터리는 고정값 95%, 컨트롤러는 첫 번째 1개만

### 설치와 사용
1. [Shizuku](https://github.com/RikkaApps/Shizuku/releases)를 설치하고 무선 디버깅 또는 ADB로 시작합니다.
2. [Releases](../../releases)에서 APK를 받아 설치합니다.
3. DualSense를 블루투스로 연결합니다.
4. 앱에서 **연결** → Shizuku 권한 허용 → **시작**.
5. 게임을 실행합니다. 슬라이더는 언제든 바꿀 수 있고 즉시 반영됩니다.
   대부분의 게임은 바로 동작합니다 (확인: 데이브 더 다이브, 할로우 나이트: 실크송).

**게임에서 패드 입력이 아예 안 되면** (확인: 데드셀), 앱이 켜진 상태에서 패드를 한 번 껐다 켜세요 (PS 버튼 10초 길게 → 다시 PS 버튼).
일부 게임은 안드로이드 **컨트롤러 1번**만 쓰는데, 처음에는 1번이 조용한 원본 패드입니다. 껐다 켜면 가상 패드가 1번이 됩니다.

되돌리려면 **정지**, 앱 강제 종료, 또는 재부팅하면 됩니다. 시스템에는 아무것도 기록하지 않습니다.

### 문제 확인
```bash
adb logcat -s dsdz DeadzoneUserService ShizukuController
```
- `Permission denied opening /dev/input (SELinux)`: 기기 보안 정책이 입력 장치 접근을 막고 있습니다.
- `unknown feature report 0x..`: 커널 드라이버 버전이 달라 다른 요청이 들어온 경우입니다. 이슈로 알려 주세요.
- 재부팅하면 Shizuku를 다시 시작해야 합니다.

### 라이선스
[MIT](LICENSE). Shizuku API 라이브러리(Apache-2.0, © RikkaW)를 포함합니다.
