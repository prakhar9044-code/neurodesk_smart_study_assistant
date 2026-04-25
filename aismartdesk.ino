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

  // ── Dramatic Startup Sequence ──
  // Frame 1: Welcome
  lcd.setCursor(0, 0); lcd.print("  NEURODESK AI  ");
  lcd.setCursor(0, 1); lcd.print("  Booting up... ");
  delay(700);

  // Frame 2: Loading bar animation
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Loading systems ");
  lcd.setCursor(0, 1); lcd.print("[");
  for (int i = 0; i < 12; i++) {
    lcd.setCursor(1 + i, 1); lcd.print("="); delay(90);
  }
  lcd.setCursor(13, 1); lcd.print("]OK");
  beepStartup();
  delay(400);

  // Frame 3: Sensor check
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DHT22  ..... OK ");
  delay(280);
  lcd.setCursor(0, 1); lcd.print("MQ135  ..... OK ");
  delay(280);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("LDR    ..... OK ");
  delay(280);
  lcd.setCursor(0, 1); lcd.print("RGB    ..... OK ");
  delay(400);

  // Frame 4: Ready!
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("* NEURODESK AI *");
  lcd.setCursor(0, 1); lcd.print(" System  Ready! ");
  blinkBacklight(3);
  delay(1000);
  lcd.clear();

  Serial.println("Smart Study Desk - Ready!");
}


// ============================================================
//  TRIGGER ALERT (buzzer + LED + LCD message)
// ============================================================

// Random posture messages for variety
String getPostureMsg() {
  int r = random(0, 6);
  switch (r) {
    case 0: return "Sit up straight!";
    case 1: return "Back straight!  ";
    case 2: return "Chin up! Posture";
    case 3: return "Spine check now!";
    case 4: return "Roll shoulders! ";
    case 5: return "Fix posture now!";
    default:return "Sit up straight!";
  }
}

// Random phone messages
String getPhoneMsg() {
  int r = random(0, 5);
  switch (r) {
    case 0: return "Phone down! Now!";
    case 1: return "Focus > TikTok! ";
    case 2: return "Study first!    ";
    case 3: return "Put. It. Down.  ";
    case 4: return "No phones! Focus";
    default:return "Put phone down! ";
  }
}

void triggerAlert(char alertType) {
  lcd.clear();

  if (alertType == 'P') {
    // RED - bad posture — blink backlight + dramatic message
    blinkRGB(255, 0, 0, 2, 150, 80);
    rgbRed();
    blinkBacklight(2);
    lcd.setCursor(0, 0);
    lcd.print(">POSTURE ALERT! ");
    lcd.setCursor(0, 1);
    lcd.print(getPostureMsg());
    beepPosture();

  } else if (alertType == 'D') {
    // YELLOW - phone distraction
    blinkRGB(255, 180, 0, 1, 400, 80);
    rgbYellow();
    blinkBacklight(1);
    lcd.setCursor(0, 0);
    lcd.print(">PHONE DETECTED!");
    lcd.setCursor(0, 1);
    lcd.print(getPhoneMsg());
    beepDistraction();

  } else if (alertType == 'E') {
    // PURPLE - environment alert with specific gas/temp message
    blinkRGB(180, 0, 255, 3, 100, 60);
    rgbPurple();
    blinkBacklight(3);
    lcd.setCursor(0, 0);
    lcd.print(">ENV WARNING!   ");
    lcd.setCursor(0, 1);
    if      (lastGasValue > GAS_THRESHOLD) lcd.print("Open window NOW!");
    else if (lastTemp     > TEMP_MAX)      lcd.print("Too hot! Cool it");
    else                                   lcd.print("High humidity!  ");
    beepEnvironment();

  } else if (alertType == 'L') {
    // ORANGE - low light
    blinkRGB(255, 80, 0, 2, 200, 100);
    rgbOrange();
    blinkBacklight(2);
    lcd.setCursor(0, 0);
    lcd.print(">LOW LIGHT!     ");
    lcd.setCursor(0, 1);
    lcd.print("Turn on a lamp! ");
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
//  LCD ANIMATION HELPERS
// ============================================================

// Scrolls a message across row 1 of the LCD
void scrollMessage(String msg) {
  String padded = "                " + msg + "                ";
  for (int i = 0; i <= (int)(padded.length() - 16); i++) {
    lcd.setCursor(0, 1);
    lcd.print(padded.substring(i, i + 16));
    delay(200);
  }
}

// Types a message letter by letter on a given row
void typeMessage(int row, String msg) {
  lcd.setCursor(0, row);
  for (int i = 0; i < (int)msg.length() && i < 16; i++) {
    lcd.print(msg[i]);
    delay(45);
  }
}

// Blinks the backlight 3 times for emphasis
void blinkBacklight(int times) {
  for (int i = 0; i < times; i++) {
    lcd.noBacklight(); delay(150);
    lcd.backlight();   delay(150);
  }
}

// Prints a progress bar on row 1: [========  ] 80%
void progressBar(int percent) {
  int filled = map(percent, 0, 100, 0, 10);
  lcd.setCursor(0, 1);
  lcd.print("[");
  for (int i = 0; i < 10; i++) lcd.print(i < filled ? "=" : " ");
  lcd.print("] ");
  lcd.print(percent);
  lcd.print("%  ");
}


// ============================================================
//  MOTIVATIONAL QUOTES (cycles through 8 quotes)
// ============================================================

String getMotivation(int index) {
  switch (index % 8) {
    case 0: return "Stay focused!   ";
    case 1: return "You got this!   ";
    case 2: return "Deep work = win!";
    case 3: return "No phone zone!  ";
    case 4: return "Brain at 100%!  ";
    case 5: return "Knowledge++     ";
    case 6: return "Focus. Flow. Win";
    case 7: return "NEURODESK ON!   ";
    default:return "Keep going!     ";
  }
}

int motivationIndex = 0;


// ============================================================
//  NORMAL LCD DISPLAY (cycles between 5 screens)
// ============================================================

void updateLCDNormal() {
  lcd.clear();

  if (lcdScreen == 0) {
    // Screen 0: Animated temp display with emoji-style indicator
    lcd.setCursor(0, 0);
    if      (lastTemp > 35) lcd.print("!! HOT  ");
    else if (lastTemp < 18) lcd.print("COLD    ");
    else                    lcd.print("TEMP OK ");
    lcd.print(lastTemp, 1); lcd.print("ß""C  ");

    // Progress bar showing temp on scale 15-40C
    int tempPct = constrain(map((int)lastTemp, 15, 40, 0, 100), 0, 100);
    progressBar(tempPct);

  } else if (lcdScreen == 1) {
    // Screen 1: Humidity with comfort rating
    lcd.setCursor(0, 0);
    lcd.print("HUMIDITY: ");
    lcd.print(lastHumidity, 1); lcd.print("%  ");

    lcd.setCursor(0, 1);
    if      (lastHumidity < 25) lcd.print(">> Too Dry! <<  ");
    else if (lastHumidity < 40) lcd.print("Comfort: Good   ");
    else if (lastHumidity < 60) lcd.print("Comfort: Great! ");
    else if (lastHumidity < 75) lcd.print("Comfort: Humid  ");
    else                        lcd.print(">> Too Humid!<< ");

  } else if (lcdScreen == 2) {
    // Screen 2: Air quality with animated label
    lcd.setCursor(0, 0);
    lcd.print("AIR IDX: "); lcd.print(lastGasValue); lcd.print("    ");

    lcd.setCursor(0, 1);
    if      (lastGasValue < 150) lcd.print("** PURE AIR! ** ");
    else if (lastGasValue < 250) lcd.print("Air: Excellent  ");
    else if (lastGasValue < 400) lcd.print("Air: Good ->OK  ");
    else if (lastGasValue < 600) lcd.print("!! OPEN WINDOW! ");
    else                         lcd.print("!!! DANGER !!!  ");

  } else if (lcdScreen == 3) {
    // Screen 3: Light level with suggestion
    lcd.setCursor(0, 0);
    lcd.print("LIGHT: "); lcd.print(lastLightAO); lcd.print("      ");

    lcd.setCursor(0, 1);
    if      (lastLightAO < 100) lcd.print("Perfect Light!  ");
    else if (lastLightAO < 300) lcd.print("Light: Good     ");
    else if (lastLightAO < 600) lcd.print("Light: Dim      ");
    else                        lcd.print("Turn on a lamp! ");

  } else {
    // Screen 4: Motivational message with typing effect
    lcd.setCursor(0, 0);
    lcd.print("* NEURODESK AI *");
    lcd.setCursor(0, 1);
    lcd.print(getMotivation(motivationIndex));
    motivationIndex++;
  }

  lcdScreen = (lcdScreen + 1) % 5;   // cycle 0 → 4
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
