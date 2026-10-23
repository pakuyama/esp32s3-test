// XIAO ESP32-S3: touch D0 (GPIO1 / TOUCH1) -> USER_LED (GPIO21, active LOW) on
// On ESP32-S3 the touchRead() value goes UP when touched.
#define TOUCH_PIN D0
#define LED_PIN   LED_BUILTIN

uint32_t threshold;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED off

  // Calibrate untouched baseline (don't touch the pin during boot)
  uint64_t sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(10);
  }
  uint32_t baseline = sum / 20;
  threshold = baseline + baseline / 5;  // +20%
  Serial.printf("baseline=%u threshold=%u\n", baseline, threshold);
}

void loop() {
  uint32_t value = touchRead(TOUCH_PIN);
  bool touched = value > threshold;
  digitalWrite(LED_PIN, touched ? LOW : HIGH);
  Serial.printf("touch=%u %s\n", value, touched ? "ON" : "OFF");
  delay(50);
}
