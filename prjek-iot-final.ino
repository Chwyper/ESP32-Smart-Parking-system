#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "NAMA_WIFI_KAMU";
const char* password = "PASSWORD_WIFI_KAMU";
const char* serverUrl = "https://smart-parking-app.vercel.app/api/gate/update"; // Ganti sesuai endpoint API kamu

// RFID
#define RST_PIN     22
#define SS_PIN      21

// Servo
#define SERVO_PIN   2
#define TOUCH_PIN   14

// Ultrasonic
#define TRIG_PIN        5
#define ECHO_PIN        17
#define TRIG_PIN_OUT    12
#define ECHO_PIN_OUT    13

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Bisa juga 0x3F tergantung modul

bool isOpen = false;
unsigned long openTime = 0;
bool ultrasonicDetected = false;
unsigned long ultrasonicStartTime = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Tempelkan kartu RFID...");

  myServo.attach(SERVO_PIN);
  myServo.write(0); // posisi awal tertutup

  Wire.begin(4, 15);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistem Parkir");
  lcd.setCursor(0, 1);
  lcd.print("Siap Digunakan");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN_OUT, OUTPUT);
  pinMode(ECHO_PIN_OUT, INPUT);

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung!");
}

void loop() {
  // ===== Touch Sensor =====
  if (digitalRead(TOUCH_PIN) == HIGH) {
    Serial.println("Touch detected: Ready Car");
    delay(200);
  }

  // ===== RFID Deteksi =====
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Serial.println("Kartu RFID terdeteksi!");
    String uid = getUID();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kartu Terdeteksi");
    lcd.setCursor(0, 1);
    lcd.print(uid);

    if (!isOpen) {
      bukaServo("rfid", uid);
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // ===== Ultrasonik Masuk =====
  float distance = jarakmasuk();
  if (distance >= 2.0 && distance <= 4.5) {
    if (!ultrasonicDetected) {
      ultrasonicDetected = true;
      ultrasonicStartTime = millis();
    } else if ((millis() - ultrasonicStartTime >= 3000) && !isOpen) {
      Serial.println("Deteksi masuk valid");
      bukaServo("masuk", "");
    }
  } else {
    ultrasonicDetected = false;
  }

  // ===== Ultrasonik Keluar =====
  float distance_out = jarakkeluar();
  if (distance_out >= 2.0 && distance_out <= 4.5 && !isOpen) {
    Serial.println("Mobil keluar");
    bukaServo("keluar", "");
  }

  // ===== Tutup otomatis servo =====
  if (isOpen && (millis() - openTime >= 10000)) {
    myServo.write(0);
    isOpen = false;
    Serial.println("Servo ditutup kembali");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Gerbang tertutup");
  }

  delay(500);
}

// ===== Fungsi buka servo + kirim ke server =====
void bukaServo(String triggerSource, String uid) {
  myServo.write(90);
  isOpen = true;
  openTime = millis();
  Serial.println("Servo terbuka");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gerbang terbuka");

  // Kirim status ke server
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["gateId"] = "gate1";
    doc["trigger"] = triggerSource;
    doc["status"] = "open";
    doc["uid"] = uid;

    String jsonData;
    serializeJson(doc, jsonData);
    int httpCode = http.POST(jsonData);

    if (httpCode > 0) {
      String response = http.getString();
      Serial.println("Server response: " + response);
    } else {
      Serial.println("Gagal kirim ke server: " + String(httpCode));
    }

    http.end();
  }
}

// ===== Fungsi jarak masuk =====
float jarakmasuk() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distance = duration * 0.034 / 2;
  return distance;
}

// ===== Fungsi jarak keluar =====
float jarakkeluar() {
  digitalWrite(TRIG_PIN_OUT, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN_OUT, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN_OUT, LOW);

  long duration_out = pulseIn(ECHO_PIN_OUT, HIGH, 30000);
  float distance_out = duration_out * 0.034 / 2;
  return distance_out;
}

// ===== Fungsi ambil UID =====
String getUID() {
  String uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidString += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidString += ":";
  }
  uidString.toUpperCase();
  Serial.print("UID: ");
  Serial.println(uidString);
  return uidString;
}
