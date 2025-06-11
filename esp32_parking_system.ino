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
// Firebase Realtime Database URL - target the 'gate_events' path and append .json
// Each POST to this URL will create a new entry with a unique ID under 'gate_events'
const char *serverUrl = "https://my-app-fak-default-rtdb.asia-southeast1.firebasedatabase.app/gate_events.json";

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

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Common I2C address, could also be 0x3F

bool isOpen = false;
unsigned long openTime = 0;
bool ultrasonicDetectedEntry = false;       // Renamed for clarity
unsigned long ultrasonicStartTimeEntry = 0; // Renamed for clarity

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
        // Potentially loop here or implement other error handling
    }
}

void loop()
{
    // ===== Touch Sensor (Car Presence for Entry) =====
    if (digitalRead(TOUCH_PIN) == HIGH)
    {
        Serial.println("Touch detected: Car ready for entry");
        // Potentially add LCD message or other logic
        delay(200); // Debounce or short delay
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
        lcd.print(uid.substring(0, 16)); // Display part of UID if too long

        if (!isOpen)
        {
            bukaServo("rfid_entry", uid); // More specific trigger
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
    }

    // ===== Ultrasonic Entry Detection =====
    float distanceEntry = jarakMasuk();
    Serial.print("Jarak Masuk: ");
    Serial.println(distanceEntry);
    if (distanceEntry >= 2.0 && distanceEntry <= 15.0)
    { // Adjusted range for more reliable detection
        if (!ultrasonicDetectedEntry)
        {
            ultrasonicDetectedEntry = true;
            ultrasonicStartTimeEntry = millis();
            Serial.println("Objek terdeteksi di jalur masuk.");
        }
        else if ((millis() - ultrasonicStartTimeEntry >= 2000) && !isOpen)
        { // Wait for 2 seconds
            Serial.println("Deteksi masuk valid (ultrasonic)");
            bukaServo("ultrasonic_entry", ""); // No UID for ultrasonic trigger
            ultrasonicDetectedEntry = false;   // Reset after triggering
        }
    }
    else
    {
        if (ultrasonicDetectedEntry && (millis() - ultrasonicStartTimeEntry >= 5000))
        { // Reset if object gone for 5s
            ultrasonicDetectedEntry = false;
            Serial.println("Objek hilang dari jalur masuk (timeout).");
        }
        else if (!ultrasonicDetectedEntry)
        {
            // Object not in range, do nothing or reset timer if it was just brief
        }
    }

    // ===== Ultrasonic Exit Detection =====
    float distanceExit = jarakKeluar();
    Serial.print("Jarak Keluar: ");
    Serial.println(distanceExit);
    if (distanceExit >= 2.0 && distanceExit <= 15.0 && !isOpen)
    { // Adjusted range
        Serial.println("Mobil keluar terdeteksi (ultrasonic)");
        bukaServo("ultrasonic_exit", ""); // No UID for ultrasonic trigger
        // Add a delay or condition to prevent immediate re-triggering if needed
        delay(3000); // Wait 3 seconds before checking again to avoid re-trigger if car moves slowly
    }

    // ===== Automatic Servo Close =====
    if (isOpen && (millis() - openTime >= 7000))
    {                     // Close after 7 seconds
        myServo.write(0); // Close gate
        isOpen = false;
        Serial.println("Servo ditutup kembali (otomatis)");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Gerbang Tertutup");
        delay(1000);
        lcd.setCursor(0, 1);
        lcd.print("Siap Digunakan");
    }

    delay(100); // Shorter loop delay for responsiveness
}

// ===== Function to open servo and send data to server =====
void bukaServo(String triggerSource, String uid)
{
    myServo.write(90); // Open gate
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

    // Send status to Firebase Realtime Database
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(serverUrl); // serverUrl already includes the .json path
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> doc; // Increased size slightly just in case, 200 should be enough
        doc["gateId"] = "gate1";     // You can make this dynamic if you have multiple gates
        doc["trigger"] = triggerSource;
        doc["status"] = "open";
        if (!uid.isEmpty())
        {
            doc["uid"] = uid;
        }
        doc["timestamp"] = millis(); // Add a simple timestamp

        String jsonData;
        serializeJson(doc, jsonData);
        Serial.print("Mengirim JSON: ");
        Serial.println(jsonData);

        int httpCode = http.POST(jsonData);

        if (httpCode > 0)
        {
            String response = http.getString();
            Serial.print("Server response code: ");
            Serial.println(httpCode);
            Serial.print("Server response: ");
            Serial.println(response);
            // You might want to parse the response if Firebase sends back useful info (e.g., the name of the new node)
        }
        else
        {
            Serial.print("Gagal kirim ke server, HTTP code: ");
            Serial.println(httpCode);
            Serial.print("Error: ");
            Serial.println(http.errorToString(httpCode));
        }

        http.end();
    }
    else
    {
        Serial.println("WiFi tidak terhubung. Data tidak dikirim.");
        lcd.setCursor(0, 1);
        lcd.print("WiFi Offline");
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

    long duration = pulseIn(ECHO_PIN, HIGH, 23200); // Timeout approx 400cm (0.034 * 23200 / 2)
    float distance = duration * 0.0343 / 2;         // Speed of sound is approx 343 m/s
    if (duration == 0)
        return 999; // Indicate timeout or no echo
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
        return 999; // Indicate timeout or no echo
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
            uidString += ":"; // Add separator for readability
    }
    uidString.toUpperCase(); // Standard format for UIDs
    Serial.print("UID: ");
    Serial.println(uidString);
    return uidString;
}
