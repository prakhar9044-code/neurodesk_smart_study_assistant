// ============================================================
//  AI-Enabled Smart Study Desk - Arduino Sketch
//  Updated with:
//    - LDR Module (4-pin: VCC, GND, DO, AO)
//    - Active Buzzer Module (3-pin: VCC, GND, IN)
//
//  WIRING GUIDE:
//  ┌─────────────────────────────────────────────┐
//  │  DHT22 Sensor                               │
//  │    VCC  → 5V                                │
//  │    GND  → GND                               │
//  │    DATA → Digital Pin 3                     │
//  ├─────────────────────────────────────────────┤
//  │  MQ135 Gas Sensor                           │
//  │    VCC  → 5V                                │
//  │    GND  → GND                               │
//  │    AOUT → A0                                │
//  ├─────────────────────────────────────────────┤
//  │  LDR Module (4-pin)                         │
//  │    VCC  → 5V                                │
//  │    GND  → GND                               │
//  │    DO   → Digital Pin 6  (HIGH=dark)        │
//  │    AO   → A1             (0-1023 light lvl) │
//  ├─────────────────────────────────────────────┤
//  │  Active Buzzer Module (3-pin)               │
//  │    VCC  → 5V                                │
//  │    GND  → GND                               │
//  │    IN   → Digital Pin 9                     │
//  ├─────────────────────────────────────────────┤
//  │  LED (with 220 ohm resistor)                │
//  │    +    → Digital Pin 8                     │
//  │    -    → GND                               │
//  ├─────────────────────────────────────────────┤
//  │  16x2 I2C LCD                               │
//  │    VCC  → 5V                                │
//  │    GND  → GND                               │
//  │    SDA  → A4                                │
//  │    SCL  → A5                                │
//  └─────────────────────────────────────────────┘
// ============================================================

#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── Pin Definitions ──
#define DHTPIN          3
#define DHTTYPE         DHT22
#define RGB_R_PIN       5      // Red   leg → Pin 5  (via 220Ω resistor)
#define RGB_G_PIN       10     // Green leg → Pin 10 (via 220Ω resistor)
#define RGB_B_PIN       11     // Blue  leg → Pin 11 (via 220Ω resistor)
// Common (GND) leg → GND
#define BUZZER_PIN      9      // Active buzzer module IN pin
#define GAS_PIN         A0     // MQ135 analog out
#define LDR_DO_PIN      6      // LDR digital out (HIGH = dark)
#define LDR_AO_PIN      A1     // LDR analog out  (0=bright, 1023=dark)

// ── Thresholds ──
#define GAS_THRESHOLD      400
#define TEMP_MAX           35.0
#define HUMIDITY_MAX       70.0
#define LIGHT_DARK_VALUE   600   // AO value above this = room is too dark
#define LIGHT_BRIGHT_VALUE 100   // AO value below this = good bright light

// ── Intervals ──
#define SENSOR_READ_INTERVAL  2000
#define LCD_UPDATE_INTERVAL   3000

// ── Objects ──
DHT dht(DHTPIN, DHTTYPE);
// Change 0x27 to 0x3F if your LCD stays blank after uploading
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Global Variables ──
char          pythonSignal     = 'G';
bool          environmentAlert = false;
bool          lightAlert       = false;
float         lastTemp         = 0;
float         lastHumidity     = 0;
int           lastGasValue     = 0;
int           lastLightAO      = 0;    // 0 = very bright, 1023 = very dark
bool          lastLightDark    = false; // true = room is dark (from DO pin)
unsigned long lastSensorTime   = 0;
unsigned long lastLCDTime      = 0;
int           lcdScreen        = 0;    // cycles 0,1,2 for 3 different screens


// ============================================================
//  ACTIVE BUZZER HELPER FUNCTIONS
//  Low level trigger: LOW = ON, HIGH = OFF
//  loudBeep() rapidly toggles pin for maximum volume
// ============================================================

void buzzerOn()  { digitalWrite(BUZZER_PIN, LOW);  }
void buzzerOff() { digitalWrite(BUZZER_PIN, HIGH); }

// ── RGB LED Control ──
// Common Cathode: analogWrite(pin, 0-255), 255 = full brightness
void setRGB(int r, int g, int b) {
  analogWrite(RGB_R_PIN, r);
  analogWrite(RGB_G_PIN, g);
  analogWrite(RGB_B_PIN, b);
}

// Preset colours for each alert type
void rgbOff()         { setRGB(0,   0,   0);   }  // Off
void rgbGreen()       { setRGB(0,   255, 0);   }  // All good
void rgbRed()         { setRGB(255, 0,   0);   }  // Bad posture
void rgbYellow()      { setRGB(255, 180, 0);   }  // Phone detected
void rgbPurple()      { setRGB(180, 0,   255); }  // Environment alert
void rgbOrange()      { setRGB(255, 80,  0);   }  // Low light alert
void rgbWhite()       { setRGB(255, 255, 255); }  // Startup

// Blink RGB a given colour N times
void blinkRGB(int r, int g, int b, int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setRGB(r, g, b);
    delay(onMs);
    rgbOff();
    delay(offMs);
  }
}

// Rapidly toggles buzzer for durationMs milliseconds
// Creates strongest membrane vibration = louder sound
void loudBeep(int durationMs) {
  long endTime = millis() + durationMs;
  while (millis() < endTime) {
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(500);
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(500);
  }
  buzzerOff();
}

// Two short loud beeps - posture alert
void beepPosture() {
  loudBeep(200); delay(120);
  loudBeep(200); delay(120);
}

// One long loud beep - phone/distraction alert
void beepDistraction() {
  loudBeep(600); delay(100);
}

// Three rapid loud beeps - environment alert
void beepEnvironment() {
  loudBeep(120); delay(80);
  loudBeep(120); delay(80);
  loudBeep(120); delay(80);
}

// Two medium loud beeps - light alert
void beepLight() {
  loudBeep(300); delay(150);
  loudBeep(300); delay(150);
}

// Startup triple beep with white flash
void beepStartup() {
  rgbWhite(); loudBeep(80);  rgbOff(); delay(60);
  rgbWhite(); loudBeep(80);  rgbOff(); delay(60);
  rgbWhite(); loudBeep(220); rgbGreen();
}


// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(9600);

  pinMode(RGB_R_PIN, OUTPUT);
  pinMode(RGB_G_PIN, OUTPUT);
  pinMode(RGB_B_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LDR_DO_PIN, INPUT);   // LDR digital out is an input to Arduino

  rgbOff();
  buzzerOff();

  dht.begin();

  lcd.init();
  lcd.backlight();

  // Startup screen
  lcd.setCursor(0, 0);
  lcd.print("Smart StudyDesk ");
  lcd.setCursor(0, 1);
  lcd.print(" Initializing.. ");

  beepStartup();
  delay(2000);
  lcd.clear();

  Serial.println("Smart Study Desk - Ready!");
}


// ============================================================
//  TRIGGER ALERT (buzzer + LED + LCD message)
// ============================================================

void triggerAlert(char alertType) {
  lcd.clear();

  if (alertType == 'P') {
    // RED - bad posture
    blinkRGB(255, 0, 0, 2, 150, 80);
    rgbRed();
    lcd.setCursor(0, 0); lcd.print("!! POSTURE ALERT");
    lcd.setCursor(0, 1); lcd.print("Sit up straight!");
    beepPosture();

  } else if (alertType == 'D') {
    // YELLOW - phone distraction
    blinkRGB(255, 180, 0, 1, 400, 80);
    rgbYellow();
    lcd.setCursor(0, 0); lcd.print("!! DISTRACTION  ");
    lcd.setCursor(0, 1); lcd.print("Put phone down! ");
    beepDistraction();

  } else if (alertType == 'E') {
    // PURPLE - environment alert
    blinkRGB(180, 0, 255, 3, 100, 60);
    rgbPurple();
    lcd.setCursor(0, 0); lcd.print("!! ENV ALERT    ");
    lcd.setCursor(0, 1);
    if      (lastGasValue  > GAS_THRESHOLD) lcd.print("Bad air quality!");
    else if (lastTemp      > TEMP_MAX)      lcd.print("Temp too high!  ");
    else                                    lcd.print("High humidity!  ");
    beepEnvironment();

  } else if (alertType == 'L') {
    // ORANGE - low light
    blinkRGB(255, 80, 0, 2, 200, 100);
    rgbOrange();
    lcd.setCursor(0, 0); lcd.print("!! LOW LIGHT    ");
    lcd.setCursor(0, 1); lcd.print("Room too dark!  ");
    beepLight();
  }
}


// ============================================================
//  CLEAR ALERT
// ============================================================

void clearAlert() {
  rgbGreen();   // Green = all good
  buzzerOff();
}


// ============================================================
//  READ ALL SENSORS
// ============================================================

void checkEnvironment() {
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  int   gasValue    = analogRead(GAS_PIN);
  int   lightAO     = analogRead(LDR_AO_PIN);   // 0=bright, 1023=dark
  bool  lightDark   = digitalRead(LDR_DO_PIN);  // HIGH = dark (below threshold)

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT22 read failed!");
    return;
  }

  // Save to globals
  lastTemp      = temperature;
  lastHumidity  = humidity;
  lastGasValue  = gasValue;
  lastLightAO   = lightAO;
  lastLightDark = lightDark;

  // Print all sensor data to Serial (Python reads this)
  // Format: Temp: 24.5 C  |  Humidity: 42.0 %  |  Gas: 312  |  Light: 245  |  Dark: 0
  Serial.print("Temp: ");     Serial.print(temperature, 1);
  Serial.print(" C  |  Humidity: "); Serial.print(humidity, 1);
  Serial.print(" %  |  Gas: ");      Serial.print(gasValue);
  Serial.print("  |  Light: ");      Serial.print(lightAO);
  Serial.print("  |  Dark: ");       Serial.println(lightDark ? 1 : 0);

  // Reset alert flags
  environmentAlert = false;
  lightAlert       = false;

  // Check thresholds
  if (gasValue   > GAS_THRESHOLD)  { environmentAlert = true; Serial.println("WARNING: High gas!"); }
  if (temperature > TEMP_MAX)      { environmentAlert = true; Serial.println("WARNING: High temp!"); }
  if (humidity    > HUMIDITY_MAX)  { environmentAlert = true; Serial.println("WARNING: High humidity!"); }
  if (lightAO     > LIGHT_DARK_VALUE) { lightAlert    = true; Serial.println("WARNING: Low light!"); }
}


// ============================================================
//  NORMAL LCD DISPLAY (cycles between 3 screens)
// ============================================================

void updateLCDNormal() {
  lcd.clear();

  if (lcdScreen == 0) {
    // Screen 0: Temperature + Humidity
    lcd.setCursor(0, 0);
    lcd.print("Temp: "); lcd.print(lastTemp, 1); lcd.print(" C      ");
    lcd.setCursor(0, 1);
    lcd.print("Humid: "); lcd.print(lastHumidity, 1); lcd.print("%    ");

  } else if (lcdScreen == 1) {
    // Screen 1: Gas + Air quality label
    lcd.setCursor(0, 0);
    lcd.print("Gas: "); lcd.print(lastGasValue); lcd.print("        ");
    lcd.setCursor(0, 1);
    if      (lastGasValue < 200) lcd.print("Air: Excellent  ");
    else if (lastGasValue < 400) lcd.print("Air: Good       ");
    else                         lcd.print("Air: Poor!      ");

  } else {
    // Screen 2: Light level
    lcd.setCursor(0, 0);
    lcd.print("Light: "); lcd.print(lastLightAO); lcd.print("      ");
    lcd.setCursor(0, 1);
    if      (lastLightAO < LIGHT_BRIGHT_VALUE) lcd.print("Bright: Good    ");
    else if (lastLightAO < LIGHT_DARK_VALUE)   lcd.print("Light: OK       ");
    else                                        lcd.print("Too Dark! Alert ");
  }

  lcdScreen = (lcdScreen + 1) % 3;   // cycle 0 → 1 → 2 → 0
}


// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {

  // Read signal from Python
  if (Serial.available() > 0) {
    pythonSignal = Serial.read();
    Serial.print("Received: "); Serial.println(pythonSignal);
  }

  // Read sensors every 2 seconds
  unsigned long now = millis();
  if (now - lastSensorTime >= SENSOR_READ_INTERVAL) {
    lastSensorTime = now;
    checkEnvironment();
  }

  // Decide what to do
  if (pythonSignal == 'P') {
    triggerAlert('P');

  } else if (pythonSignal == 'D') {
    triggerAlert('D');

  } else if (environmentAlert) {
    triggerAlert('E');

  } else if (lightAlert) {
    triggerAlert('L');                 // New: light alert

  } else {
    clearAlert();
    if (now - lastLCDTime >= LCD_UPDATE_INTERVAL) {
      lastLCDTime = now;
      updateLCDNormal();
    }
  }

  delay(100);
}
