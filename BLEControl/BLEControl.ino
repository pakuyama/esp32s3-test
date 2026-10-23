// XIAO ESP32-S3 BLE peripheral
// - LED (USER_LED, GPIO21, active LOW) on/off: write "1"/"0" (text) or 0x01/0x00 to the LED characteristic
// - Touch sensor (D0 / GPIO1 / TOUCH1): read or subscribe (notify) to the Touch characteristic, text "touched,value"
// Phone: nRF Connect (Android/iOS) -> scan -> "XIAO-ESP32S3" -> connect
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2901.h>
#include <BLE2902.h>

#define TOUCH_PIN D0
#define LED_PIN   LED_BUILTIN

#define DEVICE_NAME  "XIAO-ESP32S3"
#define SERVICE_UUID "6e400000-b5a3-f393-e0a9-e50e24dc0001"
#define LED_UUID     "6e400001-b5a3-f393-e0a9-e50e24dc0001"
#define TOUCH_UUID   "6e400002-b5a3-f393-e0a9-e50e24dc0001"

const uint32_t NOTIFY_MS = 500;  // send touch value at least this often while subscribed

BLECharacteristic *ledChar;
BLECharacteristic *touchChar;
bool connected = false;
bool ledOn = false;
uint32_t threshold;

void applyLed() {
  digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
  ledChar->setValue(ledOn ? "1" : "0");
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    connected = true;
    Serial.println("Phone connected");
  }
  void onDisconnect(BLEServer *server) override {
    connected = false;
    Serial.println("Phone disconnected, advertising again");
    server->startAdvertising();
  }
};

class LedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (!v.length()) return;
    // Accept text ("1", "on") or a raw byte (0x01)
    ledOn = v[0] == '1' || v[0] == 1 || v.equalsIgnoreCase("on");
    applyLed();
    Serial.printf("LED %s\n", ledOn ? "ON" : "OFF");
  }
};

void addDescription(BLECharacteristic *c, const char *text) {
  BLE2901 *d = new BLE2901();
  d->setDescription(text);
  c->addDescriptor(d);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // Calibrate untouched baseline (don't touch D0 during boot)
  uint64_t sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(10);
  }
  uint32_t baseline = sum / 20;
  threshold = baseline + baseline / 5;  // +20%

  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);

  ledChar = service->createCharacteristic(LED_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  ledChar->setCallbacks(new LedCallbacks());
  addDescription(ledChar, "LED (write 1/0)");

  touchChar = service->createCharacteristic(TOUCH_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  touchChar->addDescriptor(new BLE2902());
  addDescription(touchChar, "Touch (touched,value)");

  applyLed();
  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("BLE advertising as \"%s\"\n", DEVICE_NAME);
}

void loop() {
  uint32_t value = touchRead(TOUCH_PIN);
  bool touched = value > threshold;

  char text[24];
  snprintf(text, sizeof(text), "%d,%u", touched, value);
  touchChar->setValue(text);

  // Notify immediately on touch change, otherwise every NOTIFY_MS
  static bool lastTouched = false;
  static uint32_t lastNotify = 0;
  if (connected && (touched != lastTouched || millis() - lastNotify > NOTIFY_MS)) {
    touchChar->notify();
    lastNotify = millis();
    if (touched != lastTouched) Serial.printf("Touch %s (%u)\n", touched ? "ON" : "OFF", value);
  }
  lastTouched = touched;

  delay(50);
}
