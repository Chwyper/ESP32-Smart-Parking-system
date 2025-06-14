#define BLYNK_TEMPLATE_ID "TMPL69qn0JG8J"
#define BLYNK_TEMPLATE_NAME "SmartPark"

#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ==== BLYNK CONFIG ====
char auth[] = "DwmqG5odRhXk06ABj01NSRwVBtmhumz5";
char ssid[] = "Poco_F6";
char pass[] = "123159123";

// ==== Pin Konfigurasi ====
// RFID
#define RST_PIN 22
#define SS_PIN 21

// Servo dan Touch
#define SERVO_PIN 2
#define TOUCH_PIN 14
#define TOUCH_PIN2 27
#define TOUCH_PIN3 33
#define TOUCH_PIN4 32

// Ultrasonik
#define TRIG_PIN 5
#define ECHO_PIN 17
#define TRIG_PIN_OUT 12
#define ECHO_PIN_OUT 13

// I2C LCD - custom SDA & SCL
#define SDA_LCD 25
#define SCL_LCD 26

// ==== Objek ====
MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Alamat umum 0x27

// ==== Variabel ====
bool isOpen = false;
unsigned long openTime = 0;
bool ultrasonicDetected = false;
unsigned long ultrasonicStartTime = 0;
bool slotTerisi[4] = {false, false, false, false};

// ==== Setup ====
void setup()
{
    Serial.begin(115200);
    SPI.begin();
    rfid.PCD_Init();

    Wire.begin(SDA_LCD, SCL_LCD);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Sistem Parkir");
    lcd.setCursor(0, 1);
    lcd.print("Smart Dimulai");
    delay(2000);
    lcd.clear();

    myServo.attach(SERVO_PIN);
    myServo.write(0); // posisi awal tertutup

    pinMode(TOUCH_PIN, INPUT);
    pinMode(TOUCH_PIN2, INPUT);
    pinMode(TOUCH_PIN3, INPUT);
    pinMode(TOUCH_PIN4, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(TRIG_PIN_OUT, OUTPUT);
    pinMode(ECHO_PIN_OUT, INPUT);

    Blynk.begin(auth, ssid, pass);

    Serial.println("Sistem Siap. Tempelkan kartu RFID...");
}

// ==== Loop Utama ====
void loop()
{
    Blynk.run();

    // Update status slot
    slotTerisi[0] = digitalRead(TOUCH_PIN) == HIGH;
    slotTerisi[1] = digitalRead(TOUCH_PIN2) == HIGH;
    slotTerisi[2] = digitalRead(TOUCH_PIN3) == HIGH;
    slotTerisi[3] = digitalRead(TOUCH_PIN4) == HIGH;

    updateBlynkSlotStatus();

    // === Deteksi RFID ===
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
    {
        Serial.println("Kartu RFID terdeteksi!");
        printUIDToSerial();

        int slot = cariSlotKosong();
        if (slot != -1 && !isOpen)
        {
            bukaServo();
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Masuk: Slot ");
            lcd.print(slot + 1);
            lcd.setCursor(0, 1);
            lcd.print("Silakan Parkir");

            Blynk.virtualWrite(V5, "RFID: Slot " + String(slot + 1));
        }
        else
        {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Parkiran Penuh");
            lcd.setCursor(0, 1);
            lcd.print("Tunggu Kosong");

            Blynk.virtualWrite(V5, "RFID: Penuh");
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
    }

    // === Ultrasonik Masuk ===
    float distance = jarakmasuk();
    if (distance >= 6.0 && distance <= 9.0)
    {
        if (!ultrasonicDetected)
        {
            ultrasonicDetected = true;
            ultrasonicStartTime = millis();
        }

        if ((millis() - ultrasonicStartTime >= 3000) && !isOpen)
        {
            int slot = cariSlotKosong();
            if (slot != -1)
            {
                bukaServo();
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Sensor: Slot ");
                lcd.print(slot + 1);
                lcd.setCursor(0, 1);
                lcd.print("Silakan Masuk");

                Blynk.virtualWrite(V5, "Sensor: Slot " + String(slot + 1));
            }
            else
            {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Parkiran Penuh");
                lcd.setCursor(0, 1);
                lcd.print("Tunggu Kosong");

                Blynk.virtualWrite(V5, "Sensor: Penuh");
            }
            ultrasonicDetected = false;
        }
    }
    else
    {
        ultrasonicDetected = false;
    }

    // === Ultrasonik Keluar ===
    float distance_out = jarakkeluar();
    if (distance_out >= 2.0 && distance_out <= 4.5 && !isOpen)
    {
        bukaServo();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Terima Kasih");
        lcd.setCursor(0, 1);
        lcd.print("Hati-hati!");

        Blynk.virtualWrite(V5, "Keluar: Terima Kasih");
    }

    // === Tutup Otomatis ===
    if (isOpen && (millis() - openTime >= 10000))
    {
        myServo.write(0);
        isOpen = false;
        Serial.println("Servo ditutup");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Gerbang Ditutup");

        Blynk.virtualWrite(V5, "Gerbang Ditutup");
    }

    delay(300);
}

// ==== Fungsi Tambahan ====
void bukaServo()
{
    myServo.write(90);
    isOpen = true;
    openTime = millis();
    Serial.println("Gerbang terbuka");
}

float jarakmasuk()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 30000);
    return dur * 0.034 / 2;
}

float jarakkeluar()
{
    digitalWrite(TRIG_PIN_OUT, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN_OUT, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN_OUT, LOW);
    long dur = pulseIn(ECHO_PIN_OUT, HIGH, 30000);
    return dur * 0.034 / 2;
}

int cariSlotKosong()
{
    for (int i = 0; i < 4; i++)
    {
        if (!slotTerisi[i])
            return i;
    }
    return -1;
}

void printUIDToSerial()
{
    String uidString = "";
    for (byte i = 0; i < rfid.uid.size; i++)
    {
        uidString += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uidString += String(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1)
            uidString += ":";
    }
    uidString.toUpperCase();
    Serial.print("UID: ");
    Serial.println(uidString);
}

void updateBlynkSlotStatus()
{
    Blynk.virtualWrite(V0, slotTerisi[0]);
    Blynk.virtualWrite(V1, slotTerisi[1]);
    Blynk.virtualWrite(V2, slotTerisi[2]);
    Blynk.virtualWrite(V3, slotTerisi[3]);
}

// === Blynk kontrol buka/tutup manual ===
BLYNK_WRITE(V4)
{
    int value = param.asInt();
    if (value == 1)
    {
        bukaServo();
        Blynk.virtualWrite(V5, "Manual: Buka Gerbang");
    }
    else
    {
        myServo.write(0);
        isOpen = false;
        Blynk.virtualWrite(V5, "Manual: Tutup Gerbang");
    }
}
