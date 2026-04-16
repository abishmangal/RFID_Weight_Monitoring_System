// ===== BLYNK CONFIGURATION =====
#define BLYNK_TEMPLATE_ID "TMPL3SAdiyxEx"
#define BLYNK_TEMPLATE_NAME "Smart Grocery Tracker"
#define BLYNK_AUTH_TOKEN "_dEgf13EN33hW5B8P7XfpzQBrq3EHZ2a"

// ===== INCLUDES =====
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HX711.h>

// ===== WiFi Credentials =====
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// ===== RFID Pins =====
#define SS_PIN    5
#define RST_PIN   27
#define SCK_PIN   18
#define MOSI_PIN  23
#define MISO_PIN  19

// ===== HX711 Pins =====
#define HX711_DT_PIN   16
#define HX711_SCK_PIN  17

MFRC522 mfrc522(SS_PIN, RST_PIN);
HX711 scale;

// RFID Tags Mapping
struct RFIDTag {
  String uid;
  int vPin;
};

RFIDTag tags[] = {
  {"01:02:03:04", V10},
  {"AA:BB:CC:DD", V11},
  {"11:22:33:44", V12},
  {"55:66:77:88", V13},
  {"99:00:11:22", V14}
};
int tagCount = 5;

String currentUID = "";
int currentVPin = -1;
unsigned long lastMeasurementTime = 0;
const unsigned long MEASUREMENT_INTERVAL = 10000;

BlynkTimer timer;

void setup() {
  Serial.begin(115200);
  
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();
  
  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(420);  // Your working calibration factor
  scale.tare();
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(MEASUREMENT_INTERVAL, sendWeightToBlynk);
  
  Serial.println("\n✅ RFID + Weight System Ready");
  Serial.println("Unit: Kilograms (1 decimal place precision)");
  Serial.println("Tap a registered RFID tag to start monitoring\n");
}

void loop() {
  Blynk.run();
  timer.run();
  checkForNewRFID();
  delay(50);
}

int findVPinByUID(String uid) {
  for (int i = 0; i < tagCount; i++) {
    if (tags[i].uid == uid) {
      return tags[i].vPin;
    }
  }
  return -1;
}

void checkForNewRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  
  String newUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) newUID += "0";
    newUID += String(mfrc522.uid.uidByte[i], HEX);
    newUID.toUpperCase();
    if (i < mfrc522.uid.size - 1) newUID += ":";
  }
  
  int newVPin = findVPinByUID(newUID);
  
  if (newVPin != -1) {
    if (currentUID != newUID) {
      currentUID = newUID;
      currentVPin = newVPin;
      
      Serial.print("🎫 Switched to Tag: ");
      Serial.print(currentUID);
      Serial.print(" | Using Virtual Pin: V");
      Serial.println(currentVPin - 10);
      
      sendWeightToBlynk();
    }
  } else {
    if (currentUID != "") {
      Serial.println("⚠️ Unregistered tag tapped. Monitoring stopped.");
      currentUID = "";
      currentVPin = -1;
    }
  }
  
  mfrc522.PICC_HaltA();
}

void sendWeightToBlynk() {
  if (currentVPin == -1) return;
  
  float weight_kg = scale.get_units(5);
  if (weight_kg < 0) weight_kg = 0;
  
  // Send to dedicated Virtual Pin for this RFID tag
  Blynk.virtualWrite(currentVPin, weight_kg);
  
  // Update global weight display
  Blynk.virtualWrite(V0, weight_kg);
  
  // Display locally in KG with 1 decimal
  Serial.print("📤 Sent to Blynk - Tag: ");
  Serial.print(currentUID);
  Serial.print(" | Weight: ");
  Serial.print(weight_kg, 1);  // 1 decimal place
  Serial.println(" kg");
}

// Tare command from Blynk app (V5 button)
BLYNK_WRITE(V5) {
  int value = param.asInt();
  if (value == 1) {
    Serial.println("⚖️ Tare command received from Blynk");
    scale.tare();
    sendWeightToBlynk();  // Send updated reading
  }
}
