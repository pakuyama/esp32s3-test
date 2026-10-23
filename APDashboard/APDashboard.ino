// XIAO ESP32-S3 web dashboard in access point mode (no router / internet)
// - Board hosts its own Wi-Fi (AP_SSID); join it from a phone and open http://192.168.4.1
// - LED (USER_LED, GPIO21, active LOW) on/off from the browser
// - Live touch sensor (D0 / GPIO1 / TOUCH1) status in the browser
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"

#define TOUCH_PIN D0
#define LED_PIN   LED_BUILTIN

WebServer server(80);
bool ledOn = false;
uint32_t baseline, threshold;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>XIAO ESP32-S3</title>
<style>
  :root { --bg:#f4f5f7; --card:#fff; --text:#1d2330; --muted:#6b7280; --on:#f59e0b; --off:#d1d5db; --accent:#2563eb; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#111318; --card:#1b1f27; --text:#e5e7eb; --muted:#9ca3af; --off:#374151; }
  }
  * { box-sizing:border-box; }
  body { margin:0; font-family:system-ui,-apple-system,"Segoe UI",sans-serif; background:var(--bg); color:var(--text); }
  main { max-width:480px; margin:0 auto; padding:24px 16px; }
  h1 { font-size:1.3rem; margin:0 0 4px; }
  .sub { color:var(--muted); font-size:.85rem; margin-bottom:20px; }
  .card { background:var(--card); border-radius:14px; padding:20px; margin-bottom:16px; box-shadow:0 1px 3px rgba(0,0,0,.08); }
  .card h2 { font-size:.8rem; text-transform:uppercase; letter-spacing:.06em; color:var(--muted); margin:0 0 14px; }
  .row { display:flex; align-items:center; justify-content:space-between; gap:12px; }
  .dot { width:56px; height:56px; border-radius:50%; background:var(--off); transition:.15s; }
  .dot.on { background:var(--on); box-shadow:0 0 24px var(--on); }
  .state { font-size:1.6rem; font-weight:600; }
  .btns { display:flex; gap:8px; margin-top:16px; }
  button { flex:1; padding:12px; font-size:1rem; border:0; border-radius:10px; cursor:pointer; background:var(--off); color:var(--text); }
  button.primary { background:var(--accent); color:#fff; }
  .bar { height:8px; background:var(--off); border-radius:4px; overflow:hidden; margin-top:14px; }
  .bar > div { height:100%; width:0; background:var(--accent); transition:width .1s; }
  .meta { color:var(--muted); font-size:.8rem; margin-top:8px; font-variant-numeric:tabular-nums; }
  .err { color:#dc2626; }
</style>
</head>
<body>
<main>
  <h1>XIAO ESP32-S3 대시보드</h1>
  <div class="sub" id="conn">연결 중…</div>

  <section class="card">
    <h2>LED</h2>
    <div class="row">
      <div class="state" id="ledText">-</div>
      <div class="dot" id="ledDot"></div>
    </div>
    <div class="btns">
      <button class="primary" onclick="setLed(1)">켜기</button>
      <button onclick="setLed(0)">끄기</button>
    </div>
  </section>

  <section class="card">
    <h2>터치 센서 (D0)</h2>
    <div class="row">
      <div class="state" id="touchText">-</div>
      <div class="dot" id="touchDot"></div>
    </div>
    <div class="bar"><div id="touchBar"></div></div>
    <div class="meta" id="touchMeta"></div>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
function render(s) {
  $('ledText').textContent = s.led ? '켜짐' : '꺼짐';
  $('ledDot').classList.toggle('on', s.led);
  $('touchText').textContent = s.touched ? '터치됨' : '대기';
  $('touchDot').classList.toggle('on', s.touched);
  const pct = Math.min(100, Math.max(0, (s.value - s.baseline) / (s.threshold - s.baseline) * 50));
  $('touchBar').style.width = pct + '%';
  $('touchMeta').textContent = `값 ${s.value} · 기준 ${s.threshold}`;
  $('conn').textContent = '연결됨 · ' + location.host;
  $('conn').classList.remove('err');
}
function fail() { $('conn').textContent = '연결 끊김'; $('conn').classList.add('err'); }
async function poll() {
  try { render(await (await fetch('/api/state')).json()); } catch (e) { fail(); }
  setTimeout(poll, 200);
}
async function setLed(on) {
  try { render(await (await fetch('/api/led?on=' + on, { method: 'POST' })).json()); } catch (e) { fail(); }
}
poll();
</script>
</body>
</html>
)HTML";

void applyLed() {
  digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
}

void sendState() {
  uint32_t value = touchRead(TOUCH_PIN);
  char json[160];
  snprintf(json, sizeof(json),
           "{\"led\":%s,\"touched\":%s,\"value\":%u,\"baseline\":%u,\"threshold\":%u}",
           ledOn ? "true" : "false", value > threshold ? "true" : "false",
           value, baseline, threshold);
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  applyLed();

  // Calibrate untouched baseline (don't touch D0 during boot)
  uint64_t sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(10);
  }
  baseline = sum / 20;
  threshold = baseline + baseline / 5;  // +20%

  // Access point mode: the board runs its own Wi-Fi (no router or internet needed); phone joins it
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PSWD);
  Serial.printf("AP \"%s\" up, open http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/led", HTTP_POST, [] {
    ledOn = server.arg("on") == "1";
    applyLed();
    sendState();
  });
  server.begin();
}

void loop() {
  server.handleClient();

  // Print AP address and connected clients periodically for the serial monitor
  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    Serial.printf("AP %s  http://%s  clients=%d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
  }
}
