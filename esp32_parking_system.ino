#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char *ssid = "HOSTPOT ITENAS"; // Your WiFi SSID
const char *password = "";           // Your WiFi Password (leave empty if open)
// Firebase Realtime Database URL
const char *firebaseHost = "my-app-fak-default-rtdb.asia-southeast1.firebasedatabase.app";
const String firebaseGateEventsPath = "/gate_events.json"; // For POSTing new gate events
const String firebaseParkingSlotsPath = "/parking_slots/"; // Base path for PUTting slot status, will append slotId.json

// RFID
#define RST_PIN 22
#define SS_PIN 21

// Servo
#define SERVO_PIN 2
#define TOUCH_PIN 14 // Assuming this is a touch sensor to indicate car presence for entry

// Ultrasonic
#define TRIG_PIN 5 // Ultrasonic sensor for entry detection
#define ECHO_PIN 17
#define TRIG_PIN_OUT 12 // Ultrasonic sensor for exit detection
#define ECHO_PIN_OUT 13

// Slot Sensors (TTP223) - 4 slots
const int slotSensorPins[] = {18, 19, 23, 25}; // GPIO pins for TTP223 sensors
const int numSlots = sizeof(slotSensorPins) / sizeof(slotSensorPins[0]);
bool currentSlotStatus[numSlots];
bool previousSlotStatus[numSlots];
String slotIds[numSlots] = {"slot1", "slot2", "slot3", "slot4"}; // Corresponding IDs for Firebase

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Common I2C address, could also be 0x3F

bool isOpen = false;
unsigned long openTime = 0;
bool ultrasonicDetectedEntry = false;
unsigned long ultrasonicStartTimeEntry = 0;

void setup()
{
    Serial.begin(115200);
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("Tempelkan kartu RFID...");

    myServo.attach(SERVO_PIN);
    myServo.write(0); // Initial position: closed

    Wire.begin(4, 15); // SDA, SCL pins for ESP32 I2C
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

    // Initialize slot sensor pins and initial status
    for (int i = 0; i < numSlots; i++)
    {
        pinMode(slotSensorPins[i], INPUT_PULLUP);               // Assuming TTP223 output is HIGH when touched, LOW otherwise. Use INPUT_PULLUP if it's active LOW. Adjust if needed.
        previousSlotStatus[i] = digitalRead(slotSensorPins[i]); // Read initial state
        currentSlotStatus[i] = previousSlotStatus[i];
        // Optionally, update Firebase with initial status here if needed
        // updateSlotStatusFirebase(slotIds[i], currentSlotStatus[i]);
    }

    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Menghubungkan WiFi");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    int wifiConnectAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiConnectAttempts < 20)
    { // Try for 10 seconds
        delay(500);
        Serial.print(".");
        lcd.print(".");
        wifiConnectAttempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi terhubung!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Terhubung");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP());
        delay(2000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Sistem Parkir");
        lcd.setCursor(0, 1);
        lcd.print("Siap Digunakan");
    }
    else
    {
        Serial.println("\nWiFi gagal terhubung.");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Gagal");
        lcd.setCursor(0, 1);
        lcd.print("Cek Koneksi");
    }
}

void loop()
{
    // ===== Slot Sensor Detection =====
    for (int i = 0; i < numSlots; i++)
    {
        // Assuming TTP223 gives HIGH when car is present (slot occupied)
        // and LOW when car is not present (slot empty).
        // Adjust logic if your sensor behaves differently (e.g., active LOW).
        // If using INPUT_PULLUP and sensor pulls LOW on detection, then occupied is LOW.
        bool occupied = (digitalRead(slotSensorPins[i]) == HIGH); // Change to LOW if active-low with pullup

        currentSlotStatus[i] = occupied;

        if (currentSlotStatus[i] != previousSlotStatus[i])
        {
            Serial.print("Slot ");
            Serial.print(slotIds[i]);
            Serial.print(" status berubah menjadi: ");
            Serial.println(currentSlotStatus[i] ? "TERISI" : "KOSONG");
            updateSlotStatusFirebase(slotIds[i], currentSlotStatus[i]);
            previousSlotStatus[i] = currentSlotStatus[i];
        }
    }

    // ===== Touch Sensor (Car Presence for Entry Gate) =====
    if (digitalRead(TOUCH_PIN) == HIGH)
    {
        Serial.println("Touch detected: Car ready for entry gate");
        delay(200);
    }

    // ===== RFID Detection =====
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial())
    {
        Serial.println("Kartu RFID terdeteksi!");
        String uid = getUID();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Kartu Terdeteksi");
        lcd.setCursor(0, 1);
        lcd.print(uid.substring(0, 16));

        if (!isOpen)
        {
            bukaServo("rfid_entry", uid);
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
    }

    // ===== Ultrasonic Entry Detection =====
    float distanceEntry = jarakMasuk();
    // Serial.print("Jarak Masuk: "); Serial.println(distanceEntry); // Uncomment for debugging
    if (distanceEntry >= 2.0 && distanceEntry <= 15.0)
    {
        if (!ultrasonicDetectedEntry)
        {
            ultrasonicDetectedEntry = true;
            ultrasonicStartTimeEntry = millis();
            Serial.println("Objek terdeteksi di jalur masuk (ultrasonic).");
        }
        else if ((millis() - ultrasonicStartTimeEntry >= 2000) && !isOpen)
        {
            Serial.println("Deteksi masuk valid (ultrasonic)");
            bukaServo("ultrasonic_entry", "");
            ultrasonicDetectedEntry = false;
        }
    }
    else
    {
        if (ultrasonicDetectedEntry && (millis() - ultrasonicStartTimeEntry >= 5000))
        {
            ultrasonicDetectedEntry = false;
            Serial.println("Objek hilang dari jalur masuk (timeout).");
        }
    }

    // ===== Ultrasonic Exit Detection =====
    float distanceExit = jarakKeluar();
    // Serial.print("Jarak Keluar: "); Serial.println(distanceExit); // Uncomment for debugging
    if (distanceExit >= 2.0 && distanceExit <= 15.0 && !isOpen)
    {
        Serial.println("Mobil keluar terdeteksi (ultrasonic)");
        bukaServo("ultrasonic_exit", "");
        delay(3000);
    }

    // ===== Automatic Servo Close =====
    if (isOpen && (millis() - openTime >= 7000))
    {
        myServo.write(0);
        isOpen = false;
        Serial.println("Servo ditutup kembali (otomatis)");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Gerbang Tertutup");
        delay(1000);
        lcd.setCursor(0, 1);
        lcd.print("Siap Digunakan");
    }

    delay(100);
}

// ===== Function to open servo and send gate event to server =====
void bukaServo(String triggerSource, String uid)
{
    myServo.write(90);
    isOpen = true;
    openTime = millis();
    Serial.print("Servo terbuka oleh: ");
    Serial.println(triggerSource);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Gerbang Terbuka");
    if (!uid.isEmpty())
    {
        lcd.setCursor(0, 1);
        lcd.print(uid.substring(0, 16));
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        String fullServerUrl = "https://" + String(firebaseHost) + firebaseGateEventsPath;
        http.begin(fullServerUrl);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> doc;
        doc["gateId"] = "gate1";
        doc["trigger"] = triggerSource;
        doc["status"] = "open";
        if (!uid.isEmpty())
        {
            doc["uid"] = uid;
        }
        doc["timestamp"] = millis();

        String jsonData;
        serializeJson(doc, jsonData);
        Serial.print("Mengirim JSON (gate event): ");
        Serial.println(jsonData);

        int httpCode = http.POST(jsonData);

        if (httpCode > 0)
        {
            String response = http.getString();
            Serial.print("Server response code (gate event): ");
            Serial.println(httpCode);
            Serial.print("Server response (gate event): ");
            Serial.println(response);
        }
        else
        {
            Serial.print("Gagal kirim ke server (gate event), HTTP code: ");
            Serial.println(httpCode);
            Serial.print("Error: ");
            Serial.println(http.errorToString(httpCode));
        }
        http.end();
    }
    else
    {
        Serial.println("WiFi tidak terhubung. Data (gate event) tidak dikirim.");
        lcd.setCursor(0, 1);
        lcd.print("WiFi Offline");
    }
}

// ===== Function to update slot status on Firebase =====
void updateSlotStatusFirebase(String slotId, bool isOccupied)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        // Construct URL: https://<firebaseHost>/parking_slots/<slotId>.json
        String url = "https://" + String(firebaseHost) + firebaseParkingSlotsPath + slotId + ".json";
        http.begin(url);
        http.addHeader("Content-Type", "application/json"); // Though for boolean, Firebase might not strictly need it for PUT

        String payload = isOccupied ? "true" : "false";
        Serial.print("Mengirim status slot: ");
        Serial.print(slotId);
        Serial.print(" = ");
        Serial.println(payload);
        Serial.print("URL: ");
        Serial.println(url);

        int httpCode = http.PUT(payload); // Send boolean as string

        if (httpCode > 0)
        {
            String response = http.getString();
            Serial.print("Server response code (slot update): ");
            Serial.println(httpCode);
            Serial.print("Server response (slot update): ");
            Serial.println(response);
        }
        else
        {
            Serial.print("Gagal update status slot ke server, HTTP code: ");
            Serial.println(httpCode);
            Serial.print("Error: ");
            Serial.println(http.errorToString(httpCode));
        }
        http.end();
    }
    else
    {
        Serial.println("WiFi tidak terhubung. Status slot tidak dikirim.");
    }
}

// ===== Function for entry distance =====
float jarakMasuk()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 23200);
    float distance = duration * 0.0343 / 2;
    if (duration == 0)
        return 999;
    return distance;
}

// ===== Function for exit distance =====
float jarakKeluar()
{
    digitalWrite(TRIG_PIN_OUT, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN_OUT, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN_OUT, LOW);

    long duration_out = pulseIn(ECHO_PIN_OUT, HIGH, 23200);
    float distance_out = duration_out * 0.0343 / 2;
    if (duration_out == 0)
        return 999;
    return distance_out;
}

// ===== Function to get UID =====
String getUID()
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
    return uidString;
}
