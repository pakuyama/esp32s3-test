# esp32s3 test

Seeed Studio **XIAO ESP32-S3 Sense** 보드로 LED 켜기/끄기와 터치 센서(D0) 값 보기를 여러 가지 방법으로 구현한 실습 모음입니다.
작업일: 2026-10-23

## 한눈에 보기

| # | 방식 | 폴더 | 어디서 되나 | 결과 |
|---|---|---|---|---|
| 1 | 기본 동작 | `Blink/`, `TouchLED/` | 보드 단독 | LED 깜빡임, D0 터치 시 LED 켜짐 |
| 2 | 같은 와이파이 웹 대시보드 | `WebDashboard/` | 같은 공유기 안 | 보드가 웹 서버 호스팅 (`http://xiao.local`) |
| 3 | 외부망 원격 제어 | `SupabaseLED/` `dashboard/` `supabase/` | 인터넷 어디서나 | Supabase 경유 LED 제어·터치 확인·카메라(약 3fps) |
| 4 | Tailscale 실시간 카메라 | `relay/` | Tailscale 기기 | PC 중계 서버 방식으로 구성 (속도 측정 전 중단) |
| 5 | AP 모드 | `APDashboard/` | 보드 와이파이에 직접 접속 | 인터넷 없이 `http://192.168.4.1` |
| 6 | 블루투스(BLE) | `BLEControl/` `ble-dashboard/` | 휴대폰 근처 | Web Bluetooth 대시보드 배포 |

## 1. 기본 동작
- **Blink**: 보드의 사용자 LED(GPIO21, LOW일 때 켜짐) 깜빡이기
- **TouchLED**: D0(GPIO1) 터치 센서를 만지면 LED 켜짐. 부팅할 때 평균값을 재서 기준값(+20%)을 정함 → 부팅 중에는 D0를 만지지 말 것

## 2. 같은 와이파이 웹 대시보드 (`WebDashboard`)
- 보드가 공유기에 접속해 웹 서버를 띄움. 브라우저에서 LED 켜기/끄기, 터치 상태를 0.2초마다 표시
- 와이파이 연결이 안 되면 20초마다 상태 코드와 주변 와이파이 목록을 시리얼에 출력

## 3. Supabase로 외부망 원격 제어 (`SupabaseLED` + `dashboard`)
보드와 브라우저가 모두 Supabase에 접속하는 구조라 **서로 다른 네트워크에 있어도 동작**합니다.

```
[브라우저 대시보드] ⇄ [Supabase devices 테이블 / Realtime] ⇄ [ESP32]
```

- **테이블** `public.devices` (`supabase/schema.sql`): `led`, `camera`(대시보드가 설정), `touched`, `touch_value`, `ip`(보드가 보고), `updated_at`
- **보드**: 0.3초마다 LED 상태를 읽고, 터치가 바뀌거나 5초가 지나면 터치 값을 보고 (→ 대시보드의 온라인 표시)
- **대시보드**: Realtime으로 변경을 즉시 받음, 15초 동안 보고가 없으면 "오프라인" 표시
- **카메라**: '스트리밍 시작'을 누르면 보드가 JPEG를 Realtime broadcast로 전송. 테스트 결과 **640×480, 약 2.7fps, 프레임당 약 11KB**. 탭을 닫으면 자동으로 꺼짐
- 빌드 설정: **PSRAM = OPI PSRAM** 필수 (`esp32:esp32:XIAO_ESP32S3:PSRAM=opi`)
- 주의: 공개 키(publishable key)만 있으면 누구나 LED를 조작할 수 있는 데모용 권한 설정임

## 4. Tailscale 실시간 카메라 (`relay`)
ESP32에는 Tailscale을 설치할 수 없어서, PC가 중간에서 영상을 받아 Tailscale로 내보내는 구조로 만들었습니다.

```
[ESP32] ─HTTP(토큰)→ [PC relay.mjs :8090] ─Tailscale Serve(:8443, tailnet 전용)→ [휴대폰]
```

- `relay/relay.mjs`: 보드가 `/push`로 프레임을 올리면 `/stream`(MJPEG)으로 여러 명에게 송출. 보는 사람이 없으면 보드가 1초에 1장만 보내 대역폭 절약
- 시도 과정
  1. 서브넷 라우터 → PC가 보드와 다른 네트워크(유선)라 불가
  2. Tailscale Funnel → 공개 DNS에 주소가 등록되지 않아 보드가 접속 실패 (원인 미해결)
  3. **PC 공인 IP + 방화벽 8090 포트 개방** → 보드 프레임 수신 확인 (실제 시청 속도는 측정 전 중단)
- 보드 → PC 구간은 암호화되지 않음 (토큰으로만 보호)

## 5. AP 모드 (`APDashboard`)
- 보드가 직접 와이파이 `XIAO-ESP32S3`(비밀번호 `12345678`)를 만듦 → 공유기·인터넷 필요 없음
- 휴대폰으로 접속 후 **http://192.168.4.1** → LED 제어, 터치 값 확인
- 휴대폰에 "인터넷 연결 없음"이 뜨면 **연결 유지**를 선택

## 6. 블루투스 BLE (`BLEControl` + `ble-dashboard`)
- 보드가 `XIAO-ESP32S3` 이름으로 BLE 광고

| 특성 UUID | 기능 |
|---|---|
| `6e400001-b5a3-f393-e0a9-e50e24dc0001` | LED: `1`/`0` 쓰기 |
| `6e400002-b5a3-f393-e0a9-e50e24dc0001` | 터치: `터치여부,값` 읽기·알림(0.5초마다, 바뀌면 즉시) |

- **웹 대시보드 배포**: https://ble-dashboard-hazel.vercel.app (Vercel)
  - 안드로이드·PC는 Chrome/Edge, 아이폰은 Bluefy 앱에서 열기
  - '보드 연결' → `XIAO-ESP32S3` 선택 → LED 버튼·터치 실시간 표시
- nRF Connect 앱으로도 직접 확인 가능

## 겪은 문제와 해결
| 문제 | 원인 / 해결 |
|---|---|
| 와이파이 접속 실패 | SSID가 `SK_WiFiGIGA20A3` → `SK_WiFiGIGA20A3_2.4G`로 바뀜. 수정 후 정상 |
| 가끔 와이파이가 끊김 | 신호는 강함(-52dBm), 재시도하면 연결됨. 진단 로그(상태 코드·주변 와이파이 목록) 추가 |
| 와이파이 진단 스캔 실패(-2) | 접속 시도 중에 스캔해서 발생 → 와이파이를 완전히 끈 뒤 스캔하도록 수정 |
| Tailscale Funnel 접속 불가 | 공개 DNS에 레코드가 생기지 않음 → 공인 IP + 방화벽 방식으로 우회 |

## 설치 방법
1. Arduino에 ESP32 보드 패키지 설치, 보드는 **XIAO_ESP32S3** 선택 (`SupabaseLED`는 PSRAM: OPI PSRAM)
2. 각 스케치 폴더의 `secrets.example.h`를 `secrets.h`로 복사해 값 입력 (비밀 파일은 깃에 올라가지 않음)
3. `SupabaseLED`: Supabase SQL Editor에서 `supabase/schema.sql` 실행, URL·publishable key를 `secrets.h`와 `dashboard/index.html`에 입력
4. `relay`: `relay/token.txt`에 임의의 토큰을 넣고 `node relay/relay.mjs` 실행

## 남은 일
- Tailscale 실시간 스트리밍 실제 속도 측정
- 필요 없어지면 PC 방화벽 규칙("ESP32 camera relay (TCP 8090)")과 Tailscale Serve(:8443) 정리
- Supabase 권한을 로그인 사용자만 조작하도록 강화
- `BLE.md`가 비어 있음 → 요구사항이 있으면 채우기
