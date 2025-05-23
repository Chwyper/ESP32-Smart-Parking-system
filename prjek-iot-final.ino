#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>

// RFID
#define RST_PIN     22
#define SS_PIN      21

// Servo
#define SERVO_PIN   2
#define TOUCH_PIN   14
#define TOUCH_PIN2  27 

// Ultrasonic
#define TRIG_PIN        5
#define ECHO_PIN        17
#define TRIG_PIN_OUT   12
#define ECHO_PIN_OUT   13

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;

bool isOpen = false;
unsigned long openTime = 0;

// Untuk deteksi ultrasonik masuk
bool ultrasonicDetected = false;
unsigned long ultrasonicStartTime = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Tempelkan kartu RFID...");

  myServo.attach(SERVO_PIN);
  myServo.write(0); // posisi awal tertutup

  pinMode(TOUCH_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN_OUT, OUTPUT);
  pinMode(ECHO_PIN_OUT, INPUT);
}

void loop() {
  // ===== Touch Sensor =====
  if (digitalRead(TOUCH_PIN) == HIGH) {
    Serial.println("Touch detected: Slot 1 isi");
    delay(200); // debounce
  }

  if (digitalRead(TOUCH_PIN2) == HIGH) {
    Serial.println("Touch detected: Slot 2 isi");
    delay(200); // debounce
  }

  // ===== RFID Deteksi =====
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Serial.println("Kartu RFID terdeteksi!");
    printUIDToSerial();

    if (!isOpen) {
      bukaServo();
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // ===== Ultrasonik Masuk =====
  float distance = jarakmasuk();
  Serial.print("Jarak masuk: ");
  Serial.println(distance);  // Debug distance
  
  if (distance >= 6.0 && distance <= 9.0) {
    if (!ultrasonicDetected) {
      ultrasonicDetected = true;
      ultrasonicStartTime = millis();
      Serial.println("Deteksi awal ultrasonik");
    }
    
    // Cek jika sudah 3 detik berturut-turut
    if ((millis() - ultrasonicStartTime >= 3000) && !isOpen) {
      Serial.println("Deteksi masuk valid, membuka gerbang...");
      bukaServo();
      ultrasonicDetected = false;
    }
  } else {
    ultrasonicDetected = false;
    Serial.println("Objek keluar jangkauan");
  }

  // ===== Ultrasonik Keluar =====
  float distance_out = jarakkeluar();
  Serial.print("Jarak keluar: ");
  Serial.println(distance_out);
  
  if (distance_out >= 2.0 &&  distance_out <= 4.5 && !isOpen) {
    Serial.println("Mobil keluar, membuka gerbang...");
    bukaServo();
  }

  // ===== Tutup otomatis servo setelah 10 detik =====
  if (isOpen && (millis() - openTime >= 10000)) {
    myServo.write(0);
    isOpen = false;
    Serial.println("Servo ditutup kembali");
  }

  delay(500);
}

// ===== Fungsi buka servo =====
void bukaServo() {
  myServo.write(90);  // Buka
  isOpen = true;
  openTime = millis();
  Serial.println("Servo terbuka");
}

// ===== Fungsi jarak masuk =====
float jarakmasuk() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30001);
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

// ===== Fungsi tampilkan UID ke Serial =====
void printUIDToSerial() {
  String uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidString += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidString += ":";
  }
  uidString.toUpperCase();

  Serial.print("UID: ");
  Serial.println(uidString);
}
