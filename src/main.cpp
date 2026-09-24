/*
 * STM32_DHT22_oled_pwm
 * ====================
 * ปรับความเร็วพัดลม 4-pin PWM อัตโนมัติตามอุณหภูมิจาก DHT22
 * พร้อมแสดงผลบน Serial Monitor และจอ OLED 0.96" (SSD1306, I2C)
 *
 * บอร์ด  : STM32F103C8T6 (Blue Pill) - อัปโหลดผ่าน ST-Link V2 (SWD)
 *
 * การต่อสาย
 *   DHT22   VCC  -> 3.3V        (ถ้าโมดูลมีตัวต้าน pull-up มาให้ใช้ไฟ 5V ได้)
 *   DHT22   DATA -> PB12        (ต่อ R 4.7k-10k pull-up ไป 3.3V)
 *   DHT22   GND  -> GND
 *
 *   OLED    VCC  -> 3.3V
 *   OLED    GND  -> GND
 *   OLED    SCL  -> PB6  (I2C1_SCL)
 *   OLED    SDA  -> PB7  (I2C1_SDA)
 *
 *   พัดลม 4-pin
 *     Pin1 (GND/ดำ)        -> GND ของ 12V power supply
 *     Pin2 (12V/แดง-เหลือง) -> +12V ของ 12V power supply
 *     Pin3 (Tach/เขียว)     -> ไม่ต้องต่อ (ไม่ได้อ่าน RPM)
 *     Pin4 (PWM/น้ำเงิน)    -> PA6  (สัญญาณ PWM 25kHz)
 *
 *   หมายเหตุ: สัญญาณ PWM ตามสเปก Intel เป็นระดับ 5V ถ้าพัดลมไม่ตอบสนองกับ
 *   3.3V ให้เพิ่มทรานซิสเตอร์ (NPN + R) หรือ level shifter ระหว่าง PA6 กับ Pin4
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ============================== PIN ==============================
#define DHTPIN        PB12   // ขา data ของ DHT22
#define DHTTYPE       DHT22  // ชนิดเซนเซอร์
#define FAN_PWM_PIN   PA6    // ขา PWM ของพัดลม (TIM3_CH1)

// ========================== OLED ==============================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C   // ที่อยู่ I2C ของจอ (ส่วนใหญ่ 0x3C หรือ 0x3D)

// ======================= การควบคุมพัดลม ========================
#define PWM_FREQ_HZ       25000UL   // ความถี่ PWM ของพัดลม 4-pin ตามสเปก ~25kHz

#define TEMP_MIN_C        25.0f     // อุณหภูมิเริ่มหมุน (องศาเซลเซียส)
#define TEMP_MAX_C        40.0f     // อุณหภูมิที่พัดลมหมุนเต็ม 100%
#define DUTY_MIN_PERCENT  20.0f     // duty ต่ำสุด (กันพัดลมไม่หมุนแล้วกระตุก)
#define DUTY_MAX_PERCENT  100.0f    // duty สูงสุด

#define READ_INTERVAL_MS  2000UL    // ระยะห่างการอ่าน DHT22 (ต่ำกว่า 2 วินาทีไม่แนะนำ)

// ========================== ออบเจกต์ ==========================
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

HardwareTimer *fanPwmTimer = nullptr;

// ==================== ฟังก์ชันช่วย (prototype) ====================
float   computeDuty(float tempC);
void    setFanDuty(float dutyPercent);
void    drawOled(float tempC, float hum, float duty);

// ================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== STM32 DHT22 + OLED + PWM Fan ===");

  // ---- DHT22 ----
  dht.begin();

  // ---- OLED ----
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

  // ---- PWM พัดลม (Hardware Timer, 25kHz) ----
  fanPwmTimer = new HardwareTimer(TIM3);
  fanPwmTimer->setPWM(1, FAN_PWM_PIN, PWM_FREQ_HZ, 0); // channel 1, PA6
  // สตาร์ทที่ duty ต่ำสุดให้พัดลมหมุนเบา ๆ ก่อน
  setFanDuty(DUTY_MIN_PERCENT);

  Serial.println(F("เริ่มทำงานเรียบร้อย"));
}

// ================================================================
void loop() {
  static uint32_t lastRead = 0;

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();

    // ---- อ่านค่า DHT22 ----
    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();   // หน่วยองศาเซลเซียส
    // float tempF = dht.readTemperature(true); // ถ้าต้องการฟาเรนไฮต์

    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("DHT22 อ่านไม่สำเร็จ"));
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println(F("DHT22 Error!"));
      display.display();
      return;
    }

    // ---- คำนวณ duty ตามอุณหภูมิ ----
    float duty = computeDuty(temp);
    setFanDuty(duty);

    // ---- แสดงผล Serial ----
    Serial.print(F("อุณหภูมิ: ")); Serial.print(temp, 1); Serial.print(F(" C  "));
    Serial.print(F("ความชื้น: ")); Serial.print(hum, 1); Serial.print(F(" %  "));
    Serial.print(F("Fan PWM: ")); Serial.print(duty, 0); Serial.println(F(" %"));

    // ---- แสดงผล OLED ----
    drawOled(temp, hum, duty);
  }
}

// ================================================================
// คำนวณ duty cycle จากอุณหภูมิ (เชิงเส้น + clamp)
// ================================================================
float computeDuty(float tempC) {
  if (tempC <= TEMP_MIN_C) return DUTY_MIN_PERCENT;
  if (tempC >= TEMP_MAX_C) return DUTY_MAX_PERCENT;

  float range  = TEMP_MAX_C - TEMP_MIN_C;
  float dRange = DUTY_MAX_PERCENT - DUTY_MIN_PERCENT;
  return DUTY_MIN_PERCENT + ((tempC - TEMP_MIN_C) / range) * dRange;
}

// ================================================================
// ตั้ง duty cycle ของพัดลม (0-100 %)
// ================================================================
void setFanDuty(float dutyPercent) {
  if (dutyPercent < 0)     dutyPercent = 0;
  if (dutyPercent > 100)   dutyPercent = 100;

  // setPWM(channel, pin, freq, duty%) - duty อยู่ในหน่วยเปอร์เซ็นต์
  fanPwmTimer->setPWM(1, FAN_PWM_PIN, PWM_FREQ_HZ, (uint32_t)dutyPercent);
}

// ================================================================
// วาดผลลงจอ OLED
// ================================================================
void drawOled(float tempC, float hum, float duty) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- หัวข้อ / อุณหภูมิ ---
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(F("T:"));
  display.print(tempC, 1);
  display.print(F("C"));

  // --- ความชื้น ---
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print(F("Hum : "));
  display.print(hum, 1);
  display.print(F(" %"));

  // --- Duty PWM ของพัดลม ---
  display.setCursor(0, 38);
  display.print(F("Fan PWM: "));
  display.print(duty, 0);
  display.print(F(" %"));

  // --- แถบแสดงระดับความเร็ว ---
  int barWidth = map((long)duty, 0, 100, 0, SCREEN_WIDTH);
  display.fillRect(0, SCREEN_HEIGHT - 8, barWidth, 8, SSD1306_WHITE);

  display.display();
}
