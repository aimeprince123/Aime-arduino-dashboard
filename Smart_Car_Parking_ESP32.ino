/*
  SMART CAR PARKING MANAGEMENT SYSTEM
  ESP32-WROOM-32 + 2 HC-SR04 sensors + I2C LCD + web dashboard

  IMPORTANT:
  - Replace WIFI_SSID and WIFI_PASSWORD.
  - Set TOTAL_SPACES for your parking area.
  - HC-SR04 ECHO is usually 5 V. Use a voltage divider/level shifter
    before connecting ECHO to ESP32 GPIO.
  - LCD I2C address is commonly 0x27; try 0x3F if your LCD stays blank.
  - This prototype counts vehicles passing entry/exit sensors. It does not
    identify individual parking bays.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- USER SETTINGS ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const int TOTAL_SPACES = 10;

// ---------- PIN SETTINGS ----------
const int ENTRY_TRIG_PIN = 5;
const int ENTRY_ECHO_PIN = 18;
const int EXIT_TRIG_PIN  = 17;
const int EXIT_ECHO_PIN  = 16;

const int RED_LED_PIN = 26;
const int GREEN_LED_PIN = 27;

const int LCD_SDA_PIN = 21;
const int LCD_SCL_PIN = 22;

// ---------- TIMING SETTINGS ----------
const float DETECT_DISTANCE_CM = 20.0;
const unsigned long SENSOR_COOLDOWN_MS = 1800;
const unsigned long SENSOR_TIMEOUT_US = 30000;

// ---------- OBJECTS AND STATE ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

int occupiedSpaces = 0;
unsigned long lastEntryTime = 0;
unsigned long lastExitTime = 0;
unsigned long lastLcdUpdate = 0;

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Smart Parking Dashboard</title>
  <style>
    body{font-family:Arial,sans-serif;margin:0;background:#f1f5f9;color:#172033}
    header{background:#123b63;color:white;padding:22px 16px;text-align:center}
    main{max-width:760px;margin:20px auto;padding:0 14px}
    .grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
    .card{background:white;border-radius:14px;padding:20px;box-shadow:0 2px 9px #0001}
    .wide{grid-column:1/-1}
    .label{color:#64748b;font-size:14px}
    .number{font-size:38px;font-weight:700;margin-top:8px}
    #status{font-size:22px;font-weight:bold}
    .bar{height:12px;background:#e2e8f0;border-radius:20px;overflow:hidden;margin-top:14px}
    #fill{height:100%;width:0;background:#16a34a;transition:width .2s}
    footer{text-align:center;color:#64748b;font-size:12px;margin:20px}
    @media(max-width:420px){.number{font-size:30px}.card{padding:15px}}
  </style>
</head>
<body>
<header><h1>Smart Parking</h1><div>Real-time parking availability</div></header>
<main>
  <div class="grid">
    <div class="card"><div class="label">Total spaces</div><div class="number" id="total">--</div></div>
    <div class="card"><div class="label">Occupied spaces</div><div class="number" id="occupied">--</div></div>
    <div class="card wide">
      <div class="label">Remaining spaces</div>
      <div class="number" id="remaining">--</div>
      <p id="status">Connecting...</p>
      <div class="bar"><div id="fill"></div></div>
    </div>
  </div>
  <p id="updated" class="label">Waiting for ESP32...</p>
</main>
<footer>ESP32 Smart Car Parking Management System</footer>
<script>
async function updateParking(){
  try{
    const response = await fetch('/status', {cache:'no-store'});
    if(!response.ok) throw new Error('HTTP ' + response.status);
    const d = await response.json();
    document.getElementById('total').textContent = d.total;
    document.getElementById('occupied').textContent = d.occupied;
    document.getElementById('remaining').textContent = d.remaining;
    const status = document.getElementById('status');
    const fill = document.getElementById('fill');
    if(d.remaining <= 0){
      status.textContent = 'PARKING FULL';
      status.style.color = '#dc2626';
      fill.style.background = '#dc2626';
    } else {
      status.textContent = 'SPACES AVAILABLE';
      status.style.color = '#16a34a';
      fill.style.background = '#16a34a';
    }
    const used = d.total > 0 ? (d.occupied / d.total) * 100 : 0;
    fill.style.width = Math.max(0, Math.min(100, used)) + '%';
    document.getElementById('updated').textContent =
      'Last updated: ' + new Date().toLocaleTimeString();
  }catch(e){
    document.getElementById('status').textContent = 'Waiting for ESP32 connection...';
    document.getElementById('status').style.color = '#b45309';
    document.getElementById('updated').textContent = 'Could not load parking data.';
  }
}
updateParking();
setInterval(updateParking, 1000);
</script>
</body>
</html>
)HTML";

float readDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(3);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, SENSOR_TIMEOUT_US);
  if (duration == 0) {
    return -1.0;  // no echo / out of range
  }
  return (duration * 0.0343f) / 2.0f;
}

bool vehicleDetected(int trigPin, int echoPin) {
  float distance = readDistanceCM(trigPin, echoPin);
  return (distance > 0 && distance <= DETECT_DISTANCE_CM);
}

void updateIndicators() {
  int remainingSpaces = TOTAL_SPACES - occupiedSpaces;

  if (remainingSpaces <= 0) {
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, HIGH);
  }
}

void updateLCD() {
  int remainingSpaces = TOTAL_SPACES - occupiedSpaces;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Free:");
  lcd.print(remainingSpaces);
  lcd.print("/");
  lcd.print(TOTAL_SPACES);

  lcd.setCursor(0, 1);
  if (remainingSpaces <= 0) {
    lcd.print("PARKING FULL");
  } else {
    lcd.print("Cars:");
    lcd.print(occupiedSpaces);
    lcd.print(" Available");
  }
}

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handleStatus() {
  int remainingSpaces = TOTAL_SPACES - occupiedSpaces;
  String json = "{";
  json += "\"total\":" + String(TOTAL_SPACES) + ",";
  json += "\"occupied\":" + String(occupiedSpaces) + ",";
  json += "\"remaining\":" + String(remainingSpaces) + ",";
  json += "\"full\":";
  json += (remainingSpaces <= 0 ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Wi-Fi connected. Dashboard: http://");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(2500);
  } else {
    Serial.println();
    Serial.println("Wi-Fi not connected. Check SSID/password and reset ESP32.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi failed");
    lcd.setCursor(0, 1);
    lcd.print("Check settings");
    delay(2000);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ENTRY_TRIG_PIN, OUTPUT);
  pinMode(ENTRY_ECHO_PIN, INPUT);
  pinMode(EXIT_TRIG_PIN, OUTPUT);
  pinMode(EXIT_ECHO_PIN, INPUT);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  digitalWrite(ENTRY_TRIG_PIN, LOW);
  digitalWrite(EXIT_TRIG_PIN, LOW);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  occupiedSpaces = 0;
  updateIndicators();
  updateLCD();

  connectToWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  Serial.println("Web server started.");
  Serial.println("Open the IP address above on a phone/computer on the same Wi-Fi.");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Read entry sensor. Count only once per cooldown interval.
  if (now - lastEntryTime >= SENSOR_COOLDOWN_MS) {
    if (vehicleDetected(ENTRY_TRIG_PIN, ENTRY_ECHO_PIN)) {
      if (occupiedSpaces < TOTAL_SPACES) {
        occupiedSpaces++;
        Serial.print("Car entered. Occupied: ");
        Serial.println(occupiedSpaces);
      } else {
        Serial.println("Entry detected, but parking is full.");
      }
      lastEntryTime = now;
      updateIndicators();
      updateLCD();
      delay(250);
    }
  }

  // Read exit sensor. Do not allow the count to go below zero.
  if (now - lastExitTime >= SENSOR_COOLDOWN_MS) {
    if (vehicleDetected(EXIT_TRIG_PIN, EXIT_ECHO_PIN)) {
      if (occupiedSpaces > 0) {
        occupiedSpaces--;
        Serial.print("Car exited. Occupied: ");
        Serial.println(occupiedSpaces);
      } else {
        Serial.println("Exit detected, but occupied count is already zero.");
      }
      lastExitTime = now;
      updateIndicators();
      updateLCD();
      delay(250);
    }
  }

  if (now - lastLcdUpdate >= 1000) {
    updateLCD();
    lastLcdUpdate = now;
  }

  delay(30);
}
