// ===== BLYNK CONFIGURATION =====
#define BLYNK_TEMPLATE_ID "TMPL69qn0JG8J"
#define BLYNK_TEMPLATE_NAME "SmartPark"
#define BLYNK_AUTH_TOKEN "DwmqG5odRhXk06ABj01NSRwVBtmhumz5"

// ===== LIBRARY IMPORT =====
#include <ESP32Servo.h>         // Library untuk kontrol servo motor
#include <SPI.h>                // Library untuk komunikasi SPI (digunakan oleh RFID)
#include <MFRC522.h>            // Library modul RFID RC522
#include <Wire.h>               // Library I2C (digunakan oleh LCD)
#include <LiquidCrystal_I2C.h>  // Library LCD I2C
#include <WiFi.h>               // Library WiFi bawaan ESP32
#include <HTTPClient.h>         // Library untuk request HTTP (ke Firebase)
#include <ArduinoJson.h>        // Library untuk format JSON
#include <BlynkSimpleEsp32.h>   // Library untuk koneksi Blynk

// ===== WIFI & FIREBASE CONFIGURATION =====
const char* ssid = "Poco_F6";  
const char* password = "123159123";  
const char* firebaseHost = "my-app-fak-default-rtdb.asia-southeast1.firebasedatabase.app"; // URL Firebase

// ===== PIN CONFIGURATION =====
#define RST_PIN 22              // Pin reset untuk RFID
#define SS_PIN 21               // Pin SS untuk RFID
#define SERVO_PIN 2             // Pin servo motor gerbang
#define TOUCH_PIN 14            // Sensor sentuh slot 1
#define TOUCH_PIN2 27           // Sensor sentuh slot 2
#define TOUCH_PIN3 33           // Sensor sentuh slot 3
#define TOUCH_PIN4 32           // Sensor sentuh slot 4
#define TRIG_PIN 5              // Trig ultrasonic masuk
#define ECHO_PIN 17             // Echo ultrasonic masuk
#define TRIG_PIN_OUT 12         // Trig ultrasonic keluar
#define ECHO_PIN_OUT 13         // Echo ultrasonic keluar
#define SDA_LCD 25              // I2C SDA LCD
#define SCL_LCD 26              // I2C SCL LCD

// ===== OBJECT DECLARATION =====
MFRC522 rfid(SS_PIN, RST_PIN);                  // Buat objek RFID
Servo myServo;                                  // Buat objek servo
LiquidCrystal_I2C lcd(0x27, 16, 2);             // Buat objek LCD 16x2 dengan alamat I2C 0x27

// ===== VARIABLE =====
bool isOpen = false;                            // Status gerbang: terbuka / tertutup
unsigned long openTime = 0;                     // Waktu gerbang dibuka
bool ultrasonicDetectedEntry = false;           // Status deteksi ultrasonic masuk
unsigned long ultrasonicStartTimeEntry = 0;     // Waktu mulai deteksi ultrasonic masuk
bool slotTerisi[4] = {false, false, false, false}; // Status 4 slot parkir
String slotIds[4] = {"slot1", "slot2", "slot3", "slot4"}; // Nama slot di Firebase

// ===== SETUP =====
void setup() {
  Serial.begin(115200);                 // Inisialisasi serial monitor
  SPI.begin();                          // Mulai SPI
  rfid.PCD_Init();                      // Inisialisasi RFID
  Wire.begin(SDA_LCD, SCL_LCD);         // Inisialisasi I2C untuk LCD
  lcd.init(); lcd.backlight();          // Inisialisasi LCD + nyalakan backlight

  lcd.setCursor(0, 0); lcd.print("Sistem Parkir");
  lcd.setCursor(0, 1); lcd.print("Smart Dimulai");
  delay(2000);
  lcd.clear();

  myServo.attach(SERVO_PIN);            // Pasang servo ke pin
  myServo.write(0);                     // Servo di posisi tertutup

  // Konfigurasi pin input
  pinMode(TOUCH_PIN, INPUT);
  pinMode(TOUCH_PIN2, INPUT);
  pinMode(TOUCH_PIN3, INPUT);
  pinMode(TOUCH_PIN4, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN_OUT, OUTPUT);
  pinMode(ECHO_PIN_OUT, INPUT);

  // Koneksi WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 0); lcd.print("WiFi Connecting");

  // Tunggu WiFi connect max 10 detik
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi Connected");
    Serial.println("\nWiFi Connected");
  } else {
    lcd.print("WiFi Failed");
    Serial.println("\nWiFi Failed");
  }

  Blynk.config(BLYNK_AUTH_TOKEN);       // Konfigurasi Blynk
  Blynk.connect();                      // Koneksi Blynk
}

// ===== LOOP =====
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();                        // Jalankan Blynk jika WiFi tersambung
  }

  bacaSlot();                           // Baca status sensor sentuh
  cekRFID();                            // Cek kartu RFID
  cekUltrasonicMasuk();                 // Cek sensor masuk
  cekUltrasonicKeluar();                // Cek sensor keluar
  tutupGerbangOtomatis();               // Tutup otomatis jika sudah waktunya
}

// ===== BACA SLOT =====
void bacaSlot() {
  slotTerisi[0] = digitalRead(TOUCH_PIN);
  slotTerisi[1] = digitalRead(TOUCH_PIN2);
  slotTerisi[2] = digitalRead(TOUCH_PIN3);
  slotTerisi[3] = digitalRead(TOUCH_PIN4);

  for (int i = 0; i < 4; i++) {
    Serial.print("Slot ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(slotTerisi[i] ? "Terisi" : "Kosong"); // Menampilkan status slot
    Blynk.virtualWrite(V0 + i, slotTerisi[i]);  // Update status di Blynk
    updateFirebaseSlot(slotIds[i], slotTerisi[i]); // Update status di Firebase
  }
}

// ===== CEK RFID =====
void cekRFID() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = getUID();                  // Ambil UID kartu
    int slot = cariSlotKosong();            // Cari slot kosong
    if (slot != -1) {
      bukaGerbang("rfid", uid, slot);       // Buka gerbang jika ada slot
    } else {
      tampilPenuh();                        // Tampil penuh di LCD
      Blynk.virtualWrite(V5, "RFID: Penuh");
    }
    rfid.PICC_HaltA();                      // Selesaikan proses RFID
    rfid.PCD_StopCrypto1();
  }
}

// ===== CEK ULTRASONIC MASUK =====
void cekUltrasonicMasuk() {
  float jarak = bacaJarak(TRIG_PIN, ECHO_PIN);
  if (jarak >= 6.0 && jarak <= 9.0) {
    if (!ultrasonicDetectedEntry) {
      ultrasonicDetectedEntry = true;
      ultrasonicStartTimeEntry = millis();
      Serial.println("Deteksi masuk: Ultrasonic aktif"); // Menampilkan deteksi masuk
    } else if (millis() - ultrasonicStartTimeEntry > 2000 && !isOpen) {
      int slot = cariSlotKosong();
      if (slot != -1) {
        bukaGerbang("ultrasonic", "", slot);
      } else {
        tampilPenuh();
        Blynk.virtualWrite(V5, "Sensor: Penuh");
      }
      ultrasonicDetectedEntry = false;
    }
  } else {
    ultrasonicDetectedEntry = false;
  }
}

// ===== CEK ULTRASONIC KELUAR =====
void cekUltrasonicKeluar() {
  float jarak = bacaJarak(TRIG_PIN_OUT, ECHO_PIN_OUT);
  if (jarak >= 2.0 && jarak <= 4.5 && !isOpen) {
    bukaGerbang("exit", "", -1);
  }
}

// ===== TUTUP GERBANG OTOMATIS =====
void tutupGerbangOtomatis() {
  if (isOpen && millis() - openTime > 7000) {  // Tutup setelah 7 detik
    myServo.write(0);
    isOpen = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Gerbang Ditutup");
    Serial.println("Gerbang Ditutup"); // Menampilkan status gerbang
    Blynk.virtualWrite(V5, "Gerbang Ditutup");
  }
}

// ===== BUKA GERBANG =====
void bukaGerbang(String trigger, String uid, int slot) {
  myServo.write(90);
  isOpen = true;
  openTime = millis();

  lcd.clear();
  lcd.setCursor(0, 0);
  if (slot >= 0) {
    lcd.print("Slot ");
    lcd.print(slot + 1);
    lcd.setCursor(0, 1);
    lcd.print("Silakan Masuk");
    Serial.print("Gerbang Dibuka untuk Slot ");
    Serial.println(slot + 1); // Menampilkan slot yang dibuka
  } else {
    lcd.print("Terima Kasih");
    lcd.setCursor(0, 1);
    lcd.print("Hati-hati!");
    Serial.println("Gerbang Dibuka: Terima Kasih"); // Menampilkan terima kasih
  }

  Blynk.virtualWrite(V5, trigger + ": Gerbang Dibuka");
  kirimFirebaseEvent(trigger, uid, slot);
}

// ===== KIRIM EVENT KE FIREBASE =====
void kirimFirebaseEvent(String trigger, String uid, int slot) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://" + String(firebaseHost) + "/gate_events.json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["gateId"] = "gate1";
    doc["trigger"] = trigger;
    doc["uid"] = uid;
    doc["slot"] = slot + 1;
    doc["status"] = "open";
    doc["timestamp"] = millis();

    String jsonData;
    serializeJson(doc, jsonData);
    http.POST(jsonData);
    http.end();
  }
}

// ===== UPDATE SLOT DI FIREBASE =====
void updateFirebaseSlot(String slotId, bool status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://" + String(firebaseHost) + "/parking_slots/" + slotId + ".json";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.PUT(status ? "true" : "false");
    http.end();
  }
}

// ===== CARI SLOT KOSONG =====
int cariSlotKosong() {
  for (int i = 0; i < 4; i++) {
    if (!slotTerisi[i]) return i;
  }
  return -1;
}

// ===== AMBIL UID RFID =====
String getUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  Serial.println("UID: " + uid); // Menampilkan UID yang dibaca
  return uid;
}

// ===== BACA JARAK ULTRASONIC =====
float bacaJarak(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return dur * 0.034 / 2;
}

// ===== TAMPILKAN PENUH DI LCD =====
void tampilPenuh() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Parkiran Penuh");
  lcd.setCursor(0, 1);
  lcd.print("Tunggu Kosong");
  Serial.println("Parkiran Penuh"); // Menampilkan status parkiran penuh
}

