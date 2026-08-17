
#include <esp_now.h>
#include <WiFi.h>
#include "key.h"

#define SWITCH_PIN 23
#define DEBOUNCE_DELAY 50
#define ACK_TIMEOUT 1000 // Timeout 1 secondo

typedef struct struct_message {
  bool switchState;
} struct_message;

struct_message msg;
struct_message incomingAck;

unsigned long lastDebounceTime = 0;
bool lastSwitchReading = HIGH;
bool switchState = HIGH;
bool lastSentState = HIGH;

unsigned long lastSentTime = 0;
bool ackReceived = false;

// MAC address dello slave

// MAc address ricevitore 80:F3:DA:62:D3:78
uint8_t slaveAddress[] = {0x80, 0xF3, 0xDA, 0x62, 0xD3, 0x78};

void onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingAck, incomingData, sizeof(incomingAck));
  if (incomingAck.switchState) {
    Serial.println("ACK ricevuto dallo slave!");
    ackReceived = true;
  }
}

void sendMessage() {
  esp_now_send(slaveAddress, (uint8_t *)&msg, sizeof(msg));
  lastSentTime = millis();
  ackReceived = false;
  Serial.println("Messaggio inviato allo slave");
}

void setup() {
  Serial.begin(9600);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address (STA): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = true;
  memcpy(peerInfo.lmk, ESP_NOW_KEY, 16);

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);
}

void loop() {
  bool reading = digitalRead(SWITCH_PIN);

  // Debounce
  if (reading != lastSwitchReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != switchState) {
      switchState = reading;

      // Invia solo quando lo switch passa da HIGH a LOW
      if (switchState == LOW && lastSentState == HIGH) {
        msg.switchState = true;
        sendMessage();
      }
      lastSentState = switchState;
    }
  }

  lastSwitchReading = reading;

  // Gestione timeout ACK
  if (!ackReceived && lastSentTime > 0 && millis() - lastSentTime >= ACK_TIMEOUT) {
    Serial.println("ACK non ricevuto: reinvio messaggio");
    sendMessage();
  }
}
