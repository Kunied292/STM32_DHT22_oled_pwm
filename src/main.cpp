#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define DHTPIN        PB12
#define DHTTYPE       DHT22  
#define FAN_PWM_PIN   PA6 

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

#define PWM_FREQ_HZ       25000UL

#define TEMP_MIN_C        25.0f
#define TEMP_MAX_C        40.0f
#define DUTY_MIN_PERCENT  20.0f
#define DUTY_MAX_PERCENT  100.0f

#define READ_INTERVAL_MS  2000UL

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

HardwareTimer *fanPwmTimer = nullptr;

float   computeDuty(float tempC);
void    setFanDuty(float dutyPercent);
void    drawOled(float tempC, float hum, float duty);

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== STM32 DHT22 + OLED + PWM Fan ===");

  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 ไม่พบ! ตรวจสาย I2C/ที่อยู่จอ"));
    for (;;) { delay(1000); }
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Booting..."));
  display.display();

  fanPwmTimer = new HardwareTimer(TIM3);
  fanPwmTimer->setPWM(1, FAN_PWM_PIN, PWM_FREQ_HZ, 0);
  setFanDuty(DUTY_MIN_PERCENT);

  Serial.println(F("เริ่มทำงานเรียบร้อย"));
}

void loop() {
  static uint32_t lastRead = 0;

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();

    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();

    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("DHT22 อ่านไม่สำเร็จ"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println(F("DHT22 Error!"));
      display.display();
      return;
    }

    float duty = computeDuty(temp);
    setFanDuty(duty);

    Serial.print(F("อุณหภูมิ: ")); Serial.print(temp, 1); Serial.print(F(" C  "));
    Serial.print(F("ความชื้น: ")); Serial.print(hum, 1); Serial.print(F(" %  "));
    Serial.print(F("Fan PWM: ")); Serial.print(duty, 0); Serial.println(F(" %"));

    drawOled(temp, hum, duty);
  }
}

float computeDuty(float tempC) {
  if (tempC <= TEMP_MIN_C) return DUTY_MIN_PERCENT;
  if (tempC >= TEMP_MAX_C) return DUTY_MAX_PERCENT;

  float range  = TEMP_MAX_C - TEMP_MIN_C;
  float dRange = DUTY_MAX_PERCENT - DUTY_MIN_PERCENT;
  return DUTY_MIN_PERCENT + ((tempC - TEMP_MIN_C) / range) * dRange;
}

void setFanDuty(float dutyPercent) {
  if (dutyPercent < 0)     dutyPercent = 0;
  if (dutyPercent > 100)   dutyPercent = 100;

  fanPwmTimer->setPWM(1, FAN_PWM_PIN, PWM_FREQ_HZ, (uint32_t)dutyPercent);
}

void drawOled(float tempC, float hum, float duty) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(F("T:"));
  display.print(tempC, 1);
  display.print(F("C"));

  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print(F("Hum : "));
  display.print(hum, 1);
  display.print(F(" %"));

  display.setCursor(0, 38);
  display.print(F("Fan PWM: "));
  display.print(duty, 0);
  display.print(F(" %"));

  int barWidth = map((long)duty, 0, 100, 0, SCREEN_WIDTH);
  display.fillRect(0, SCREEN_HEIGHT - 8, barWidth, 8, SSD1306_WHITE);

  display.display();
}
