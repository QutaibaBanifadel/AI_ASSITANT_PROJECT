#include "esp_wifi.h"
#include <WiFi.h>
#include <SD.h>
#include <esp_now.h>
#include <Audio.h>
#include <Stepper.h>

#define AUDIO_FILE "/Audio.wav"
#define CHANNEL 1

// Deepgram credentials and keywords
const char* ssid = "MU Open";
const char* password = "";
String keywordLight1 = "light";
String keywordLight2 = "on";
String keywordLight3 = "off";
String keywordMotor1 = "lock";
String keywordMotor2 = "unlock";

// Stepper Motor
const int stepsPerRevolution = 724;
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// Pins
#define LEDPin 15
#define pin_RECORD_BTN 36
#define LED 2

// Data structure
typedef struct struct_message {
  int b;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// Receiver MAC address (replace with your actual receiver)
uint8_t receiverMAC[] = {0x94, 0x54, 0xC5, 0xAF, 0x0E, 0x14};

// Audio object
Audio audio_play;

// --- Declaration for other functions ---
bool I2S_Record_Init();
bool Record_Start(String filename);
bool Record_Available(String filename, float* audiolength_sec);
String SpeechToText_Deepgram(String filename);
void Deepgram_KeepAlive();

// --- Callback for send status ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  delay(5000);
  Serial.println("Starting Deepgram Sender...");

  pinMode(LED, OUTPUT);
  pinMode(LEDPin, OUTPUT);
  pinMode(pin_RECORD_BTN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected to Wi-Fi");

  // Print and lock to current Wi-Fi channel
  uint8_t current_channel;
wifi_second_chan_t second_channel;
esp_wifi_get_channel(&current_channel, &second_channel);

Serial.print("Current Wi-Fi Channel (for ESP-NOW): ");
Serial.println(current_channel);


  // NOTE: No need to call esp_wifi_set_channel() on sender if it's staying connected

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  // Add peer
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;  // Match current
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // SD Card + Audio
  if (!SD.begin()) {
    Serial.println("ERROR - SD Card initialization failed!");
    return;
  }
  I2S_Record_Init();

  // Setup Stepper
  myStepper.setSpeed(25);
}

void loop() {
  if (digitalRead(pin_RECORD_BTN) == LOW) {
    digitalWrite(LED, HIGH);
    delay(30);
    Record_Start(AUDIO_FILE);
  }

  if (digitalRead(pin_RECORD_BTN) == HIGH) {
    digitalWrite(LED, LOW);
    float recorded_seconds;

    if (Record_Available(AUDIO_FILE, &recorded_seconds)) {
      if (recorded_seconds > 0.4) {
        digitalWrite(LED, HIGH);

        String transcription = SpeechToText_Deepgram(AUDIO_FILE);
        transcription.toLowerCase();
        Serial.println("Transcription: " + transcription);

        // Determine command based on keywords
        if (transcription.indexOf(keywordLight1) >= 0 && transcription.indexOf(keywordLight2) >= 0) {
          myData.b = 1;
          digitalWrite(LEDPin, HIGH);
        } else if (transcription.indexOf(keywordLight1) >= 0 && transcription.indexOf(keywordLight3) >= 0) {
          myData.b = 2;
          digitalWrite(LEDPin, LOW);
        } else if (transcription.indexOf(keywordMotor2) >= 0) {
          myData.b = 3;
        } else if (transcription.indexOf(keywordMotor1) >= 0) {
          myData.b = 4;
        } else {
          myData.b = 0; // no match
        }

        // Send only if valid command
        if (myData.b != 0) {
          esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&myData, sizeof(myData));
          if (result == ESP_OK) {
            Serial.println("Sent with success");
          } else {
            Serial.print("Send failed with error code: ");
            Serial.println(result);
          }
        }

        delay(2000);
      }
    }
  }

  // Deepgram keep-alive (optional)
  if (digitalRead(pin_RECORD_BTN) == HIGH && !audio_play.isRunning()) {
    static uint32_t millis_ping_before;
    if (millis() > (millis_ping_before + 5000)) {
      millis_ping_before = millis();
      digitalWrite(LED, HIGH);
      Deepgram_KeepAlive();
      digitalWrite(LED, LOW);
    }
  }
}

