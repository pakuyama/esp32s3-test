// XIAO ESP32-S3 Sense <-> Supabase
// - Reads desired LED / camera state from public.devices (set by the web dashboard)
// - Reports touch sensor (D0 / GPIO1 / TOUCH1) state to the same row
// - While camera is on, sends JPEG frames over Supabase Realtime broadcast (topic "cam:<DEVICE_ID>")
// - Serves a full-rate MJPEG stream at http://<LAN IP>:81/stream on the LAN
// - Pushes frames to the PC relay (RELAY_URL) for full-rate remote viewing over the tailnet
// Works from any network: device and browser both talk outbound to Supabase.
// Build with Tools > PSRAM: "OPI PSRAM" (FQBN esp32:esp32:XIAO_ESP32S3:PSRAM=opi)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "mbedtls/base64.h"
#include "secrets.h"

#define TOUCH_PIN D0
#define LED_PIN   LED_BUILTIN  // USER_LED GPIO21, active LOW

const uint32_t POLL_MS      = 300;   // how often to fetch LED state
const uint32_t HEARTBEAT_MS = 5000;  // report touch even if unchanged (online indicator)
const uint32_t FRAME_GAP_MS = 50;    // pause between frames (keeps Realtime message rate sane)

WiFiClientSecure tls;
HTTPClient http;
const String rowUrl = String(SUPABASE_URL) + "/rest/v1/devices?id=eq." DEVICE_ID;
const String broadcastUrl = String(SUPABASE_URL) + "/realtime/v1/api/broadcast";

uint32_t threshold;
bool ledOn = false;
bool lastTouched = false;
uint32_t lastReport = 0;
volatile bool cameraOn = false;

void applyLed() {
  digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  Serial.print("Connecting to Wi-Fi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - start > 20000) {
      // Report why it's failing, then retry
      Serial.printf("\nWi-Fi status=%d, scanning...\n", WiFi.status());
      // Fully stop the pending connect attempt, otherwise the scan fails with -2
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(200);
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks();
      Serial.printf("scan result=%d\n", n);
      for (int i = 0; i < n; i++) {
        Serial.printf("  %s  ch%d  %ddBm\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));
      }
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PSWD);
      start = millis();
    }
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
}

// XIAO ESP32-S3 Sense camera board pins
bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = 15; c.pin_d1 = 17; c.pin_d2 = 18; c.pin_d3 = 16;
  c.pin_d4 = 14; c.pin_d5 = 12; c.pin_d6 = 11; c.pin_d7 = 48;
  c.pin_xclk = 10; c.pin_pclk = 13; c.pin_vsync = 38; c.pin_href = 47;
  c.pin_sccb_sda = 40; c.pin_sccb_scl = 39;
  c.pin_pwdn = -1; c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;  // 640x480
  c.jpeg_quality = 12;           // lower = better quality, bigger frames
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// Runs on core 0 with its own TLS session so streaming doesn't block LED/touch handling
void cameraTask(void *) {
  WiFiClientSecure camTls;
  camTls.setInsecure();
  HTTPClient camHttp;
  camHttp.setReuse(true);

  static const char head[] = "{\"messages\":[{\"topic\":\"cam:" DEVICE_ID "\",\"event\":\"frame\",\"payload\":{\"jpg\":\"";
  static const char tail[] = "\"}}]}";
  uint8_t *buf = nullptr;
  size_t cap = 0;

  for (;;) {
    if (!cameraOn || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // JSON body = head + base64(jpeg) + tail, built in PSRAM
    size_t need = sizeof(head) + 4 * ((fb->len + 2) / 3) + sizeof(tail);
    if (need > cap) {
      free(buf);
      buf = (uint8_t *)ps_malloc(need);
      cap = buf ? need : 0;
    }
    size_t len = 0;
    if (buf) {
      size_t hl = sizeof(head) - 1, b64 = 0;
      memcpy(buf, head, hl);
      mbedtls_base64_encode(buf + hl, cap - hl, &b64, fb->buf, fb->len);
      memcpy(buf + hl + b64, tail, sizeof(tail) - 1);
      len = hl + b64 + sizeof(tail) - 1;
    }
    esp_camera_fb_return(fb);

    if (len && camHttp.begin(camTls, broadcastUrl)) {
      camHttp.addHeader("apikey", SUPABASE_KEY);
      camHttp.addHeader("Content-Type", "application/json");
      int code = camHttp.POST(buf, len);
      if (code != 202) Serial.printf("Frame send failed: %d\n", code);
      camHttp.end();
    }
    vTaskDelay(pdMS_TO_TICKS(FRAME_GAP_MS));
  }
}

// MJPEG stream over plain HTTP; runs in esp_http_server's own task, one client at a time per socket
#define BOUNDARY "frame"
esp_err_t streamHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char part[96];
  for (;;) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;
    int n = snprintf(part, sizeof(part), "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    esp_err_t err = httpd_resp_send_chunk(req, part, n);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (err != ESP_OK) return err;  // client went away
  }
}

void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.ctrl_port = 32769;
  httpd_handle_t server = nullptr;
  httpd_uri_t uri = { .uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = nullptr };
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &uri);
    Serial.printf("MJPEG: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  }
}

// Pushes one JPEG per request to the PC relay (relay/relay.mjs) over plain HTTP to the PC's public IP.
// The relay answers with X-Viewers; with nobody watching, only send an occasional probe frame.
void relayTask(void *) {
  WiFiClient relayTcp;
  HTTPClient relayHttp;
  relayHttp.setReuse(true);
  const char *keys[] = { "X-Viewers" };

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    int viewers = 0;
    if (relayHttp.begin(relayTcp, RELAY_URL)) {
      relayHttp.collectHeaders(keys, 1);
      relayHttp.addHeader("Content-Type", "image/jpeg");
      relayHttp.addHeader("X-Token", RELAY_TOKEN);
      int code = relayHttp.POST(fb->buf, fb->len);
      if (code == 200) viewers = relayHttp.header("X-Viewers").toInt();
      else Serial.printf("Relay push failed: %d\n", code);
      relayHttp.end();
    }
    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(viewers > 0 ? 1 : 1000));
  }
}

// Sends GET (body empty) or PATCH (body set); returns response body or "" on failure
String request(const char *method, const String &body) {
  if (!http.begin(tls, rowUrl + "&select=led,camera")) return "";
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", "Bearer " SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=representation");
  int code = http.sendRequest(method, body);
  String resp = code == 200 ? http.getString() : "";
  if (code != 200) Serial.printf("%s failed: %d %s\n", method, code, http.getString().c_str());
  http.end();
  return resp;
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
  uint32_t baseline = sum / 20;
  threshold = baseline + baseline / 5;  // +20%

  bool cameraReady = initCamera();

  connectWiFi();
  tls.setInsecure();  // skip certificate check (demo); pin Supabase's CA for production
  http.setReuse(true);

  if (cameraReady) {
    xTaskCreatePinnedToCore(cameraTask, "camera", 16384, nullptr, 1, nullptr, 0);
    startStreamServer();
    xTaskCreatePinnedToCore(relayTask, "relay", 16384, nullptr, 1, nullptr, 0);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  uint32_t value = touchRead(TOUCH_PIN);
  bool touched = value > threshold;

  // PATCH when touch changes or heartbeat is due (response also carries LED), else GET
  String resp;
  if (touched != lastTouched || millis() - lastReport > HEARTBEAT_MS) {
    char body[96];
    snprintf(body, sizeof(body), "{\"touched\":%s,\"touch_value\":%u,\"ip\":\"%s\"}",
             touched ? "true" : "false", value, WiFi.localIP().toString().c_str());
    resp = request("PATCH", body);
    if (resp.length()) {
      lastTouched = touched;
      lastReport = millis();
    }
  } else {
    resp = request("GET", "");
  }

  if (resp.indexOf("\"led\"") >= 0) {
    bool on = resp.indexOf("\"led\":true") >= 0;
    if (on != ledOn) {
      ledOn = on;
      applyLed();
      Serial.printf("LED %s\n", ledOn ? "ON" : "OFF");
    }
    bool cam = resp.indexOf("\"camera\":true") >= 0;
    if (cam != cameraOn) {
      cameraOn = cam;
      Serial.printf("Camera %s\n", cameraOn ? "ON" : "OFF");
    }
  }

  delay(POLL_MS);
}
