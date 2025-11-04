#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <MFRC522.h>

// --- WiFi ---
const char* ssid = "YourWiFiName";
const char* password = "YourWiFiPassword";

// --- ThingSpeak ---
String apiKey = "YOUR_WRITE_API_KEY";
const char* server = "http://api.thingspeak.com/update";

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- INA219 (Slot1=0x41, Slot2=0x40) ---
Adafruit_INA219 ina219_slot1(0x41);
Adafruit_INA219 ina219_slot2(0x40);

// --- RFID RC522 ---
#define SS_PIN 21
#define RST_PIN 22
MFRC522 rfid(SS_PIN, RST_PIN);

// --- Globals ---
float v1, c1, v2, c2;
int h1, h2;
String status = "Idle";
bool showAuth = false;
String authMessage = "";
unsigned long authTime = 0;

// --- Allowed UIDs ---
String validUIDs[] = {"33287736", "43f625da"}; // replace with your card UIDs

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();

  // INA219 Init
  ina219_slot1.begin();
  ina219_slot2.begin();

  // RFID Init
  SPI.begin();
  rfid.PCD_Init();

  // WiFi Init
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

// --- OLED Display ---
void showStatus() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Smart Battery Hub");
  display.println("----------------");

  if (showAuth) {
    display.setTextSize(2);
    display.setCursor(5, 25);
    display.print(authMessage);
    display.setTextSize(1);
  } else {
    display.print("S1: V="); display.print(v1, 2); display.print(" C="); display.print(c1, 2);
    display.print(" H="); display.print(h1); display.println("%");

    display.print("S2: V="); display.print(v2, 2); display.print(" C="); display.print(c2, 2);
    display.print(" H="); display.print(h2); display.println("%");

    display.println("----------------");
    display.print("Status: "); display.println(status);
  }
  display.display();
}

// --- RFID Authentication ---
void checkRFID() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toLowerCase();

    Serial.print("Card UID: ");
    Serial.println(uid);

    bool authorized = false;
    for (String valid : validUIDs) {
      if (uid == valid) {
        authorized = true;
        break;
      }
    }

    if (authorized) {
      authMessage = "Authenticated";
      status = "User OK";
    } else {
      authMessage = "Denied";
      status = "Access Denied";
    }

    showAuth = true;
    authTime = millis();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // Hide message after 3 seconds
  if (showAuth && millis() - authTime > 3000) {
    showAuth = false;
    status = "Idle";
  }
}

// --- Loop ---
void loop() {
  // Read INA219
  v1 = ina219_slot1.getBusVoltage_V();
  c1 = ina219_slot1.getCurrent_mA() / 1000.0;
  v2 = ina219_slot2.getBusVoltage_V();
  c2 = ina219_slot2.getCurrent_mA() / 1000.0;

  // Health calculation (simple from voltage)
  h1 = constrain((int)((v1 - 3.0) / (4.2 - 3.0) * 100), 0, 100);
  h2 = constrain((int)((v2 - 3.0) / (4.2 - 3.0) * 100), 0, 100);

  // RFID Check
  checkRFID();

  // OLED Update
  showStatus();

  // ThingSpeak update (only V, C, H for slots)
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = server;
    url += "?api_key=" + apiKey;
    url += "&field1=" + String(v1, 2);   // Slot 1 Voltage
    url += "&field2=" + String(c1, 2);   // Slot 1 Current
    url += "&field3=" + String(h1);      // Slot 1 Health
    url += "&field4=" + String(v2, 2);   // Slot 2 Voltage
    url += "&field5=" + String(c2, 2);   // Slot 2 Current
    url += "&field6=" + String(h2);      // Slot 2 Health

    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.println("ThingSpeak Updated");
    } else {
      Serial.println("ThingSpeak Failed");
    }
    http.end();
  }

  delay(2000);
}
