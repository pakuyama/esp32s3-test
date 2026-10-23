# esp32s3 test

Seeed Studio XIAO ESP32-S3 (Sense) experiments: LED on/off and touch sensor (D0) from the web, Supabase, BLE and more.

| Folder | What it does |
|---|---|
| `Blink/` | Blink the user LED |
| `TouchLED/` | Touch D0 to light the LED |
| `WebDashboard/` | Web dashboard hosted by the board on the home Wi-Fi (`http://xiao.local`) |
| `APDashboard/` | Same dashboard, board runs its own Wi-Fi (AP mode) → `http://192.168.4.1` |
| `SupabaseLED/` + `dashboard/` + `supabase/` | Control from any network through Supabase (REST + Realtime), camera streaming |
| `relay/` | PC relay for full-rate MJPEG camera stream, viewed over Tailscale |
| `BLEControl/` + `ble-dashboard/` | BLE peripheral + Web Bluetooth dashboard (https://ble-dashboard-hazel.vercel.app) |

## Setup

1. Install the ESP32 core for Arduino, board **XIAO_ESP32S3** (`SupabaseLED` needs **PSRAM: OPI PSRAM**).
2. In each sketch folder, copy `secrets.example.h` to `secrets.h` and fill in your values.
3. `SupabaseLED`: run `supabase/schema.sql` in your Supabase project and put the URL / publishable key in `secrets.h` and `dashboard/index.html`.
4. `relay`: put a random token in `relay/token.txt`, then `node relay/relay.mjs`.
