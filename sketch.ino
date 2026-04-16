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

// ===== Notification Settings =====
#define LOW_WEIGHT_THRESHOLD 1.0  // 1 kg threshold
#define NOTIFICATION_COOLDOWN 60000  // 1 minute cooldown

MFRC522 mfrc522(SS_PIN, RST_PIN);
HX711 scale;

// RFID Tags Mapping with RFID numbers instead of names
struct RFIDTag {
  String uid;
  int vPin;
  String rfidNumber;
  String eventName;
  unsigned long lastNotificationTime;
};

RFIDTag tags[] = {
  {"01:02:03:04", V10, "RFID 1", "low_weight_rfid1", 0},
  {"AA:BB:CC:DD", V11, "RFID 2", "low_weight_rfid2", 0},
  {"11:22:33:44", V12, "RFID 3", "low_weight_rfid3", 0},
  {"55:66:77:88", V13, "RFID 4", "low_weight_rfid4", 0},
  {"99:00:11:22", V14, "RFID 5", "low_weight_rfid5", 0}
};
int tagCount = 5;

String currentUID = "";
int currentVPin = -1;
String currentRfidNumber = "";
String currentEventName = "";
unsigned long lastMeasurementTime = 0;
const unsigned long MEASUREMENT_INTERVAL = 10000;

bool lowWeightNotified[5] = {false, false, false, false, false};

BlynkTimer timer;

void setup() {
  Serial.begin(115200);
  
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();
  
  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(420);
  scale.tare();
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timer.setInterval(MEASUREMENT_INTERVAL, sendWeightToBlynk);
  
  Serial.println("\n✅ RFID + Weight System Ready");
  Serial.println("Low weight notifications with separate events per RFID");
  Serial.println("Registered Tags:");
  for (int i = 0; i < tagCount; i++) {
    Serial.print("  ");
    Serial.print(tags[i].rfidNumber);
    Serial.print(" -> ");
    Serial.print(tags[i].uid);
    Serial.print(" -> Event: ");
    Serial.println(tags[i].eventName);
  }
  Serial.println();
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

int findTagIndexByUID(String uid) {
  for (int i = 0; i < tagCount; i++) {
    if (tags[i].uid == uid) {
      return i;
    }
  }
  return -1;
}

String findRfidNumberByUID(String uid) {
  for (int i = 0; i < tagCount; i++) {
    if (tags[i].uid == uid) {
      return tags[i].rfidNumber;
    }
  }
  return "Unknown RFID";
}

String findEventNameByUID(String uid) {
  for (int i = 0; i < tagCount; i++) {
    if (tags[i].uid == uid) {
      return tags[i].eventName;
    }
  }
  return "";
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
      currentRfidNumber = findRfidNumberByUID(newUID);
      currentEventName = findEventNameByUID(newUID);
      
      Serial.print("🎫 Switched to Tag: ");
      Serial.print(currentRfidNumber);
      Serial.print(" (");
      Serial.print(currentUID);
      Serial.print(") | Event: ");
      Serial.println(currentEventName);
      
      // Reset notification flag when switching to a new tag
      int newTagIndex = findTagIndexByUID(newUID);
      if (newTagIndex != -1) {
        lowWeightNotified[newTagIndex] = false;
      }
      
      sendWeightToBlynk();
    }
  } else {
    if (currentUID != "") {
      Serial.println("⚠️ Unregistered tag tapped. Monitoring stopped.");
      currentUID = "";
      currentVPin = -1;
      currentRfidNumber = "";
      currentEventName = "";
    }
  }
  
  mfrc522.PICC_HaltA();
}

void sendLowWeightNotification(int tagIndex, float weight) {
  unsigned long currentTime = millis();
  
  // FIX: Only check cooldown if a notification has been sent before (lastNotificationTime != 0)
  if (tags[tagIndex].lastNotificationTime != 0) {
    unsigned long timeSinceLastNotification = currentTime - tags[tagIndex].lastNotificationTime;
    if (timeSinceLastNotification < NOTIFICATION_COOLDOWN) {
      Serial.print("  ⏸️ Cooldown active for ");
      Serial.print(tags[tagIndex].rfidNumber);
      Serial.print(" - ");
      Serial.print((NOTIFICATION_COOLDOWN - timeSinceLastNotification) / 1000);
      Serial.println(" seconds remaining");
      return;
    }
  }
  
  // Send SEPARATE event for this specific RFID tag
  String notificationMsg = "⚠️ LOW WEIGHT! " + tags[tagIndex].rfidNumber + " is at " + String(weight, 1) + " kg";
  
  // Use the specific event name for this RFID
  Blynk.logEvent(tags[tagIndex].eventName, notificationMsg);
  
  // Also send to a virtual pin for dashboard display
  Blynk.virtualWrite(V6, notificationMsg);
  
  // Update last notification time
  tags[tagIndex].lastNotificationTime = currentTime;
  
  Serial.print("🔔 NOTIFICATION SENT [");
  Serial.print(tags[tagIndex].eventName);
  Serial.print("]: ");
  Serial.println(notificationMsg);
}

void sendWeightToBlynk() {
  if (currentVPin == -1) return;
  
  float weight_kg = scale.get_units(5);
  if (weight_kg < 0) weight_kg = 0;
  
  int tagIndex = findTagIndexByUID(currentUID);
  
  // Send to dedicated Virtual Pin
  Blynk.virtualWrite(currentVPin, weight_kg);
  Blynk.virtualWrite(V0, weight_kg);
  
  // Display locally
  Serial.print("📤 ");
  Serial.print(currentRfidNumber);
  Serial.print(": ");
  Serial.print(weight_kg, 1);
  Serial.println(" kg");
  
  // Check low weight condition
  if (weight_kg < LOW_WEIGHT_THRESHOLD && weight_kg > 0) {
    if (!lowWeightNotified[tagIndex]) {
      // First time below threshold - send notification
      sendLowWeightNotification(tagIndex, weight_kg);
      lowWeightNotified[tagIndex] = true;
    } else {
      // Already notified - check cooldown for subsequent notifications
      sendLowWeightNotification(tagIndex, weight_kg);
    }
  } else {
    if (lowWeightNotified[tagIndex]) {
      lowWeightNotified[tagIndex] = false;
      // Reset lastNotificationTime when weight is restored
      tags[tagIndex].lastNotificationTime = 0;
      String recoveryMsg = "✅ " + currentRfidNumber + " restored to " + String(weight_kg, 1) + " kg";
      Blynk.virtualWrite(V6, recoveryMsg);
      Serial.print("📢 Recovery: ");
      Serial.println(recoveryMsg);
    }
  }
}

// Tare command from Blynk app (V5 button)
BLYNK_WRITE(V5) {
  int value = param.asInt();
  if (value == 1) {
    Serial.println("⚖️ Tare command received from Blynk");
    scale.tare();
    sendWeightToBlynk();
  }
}
