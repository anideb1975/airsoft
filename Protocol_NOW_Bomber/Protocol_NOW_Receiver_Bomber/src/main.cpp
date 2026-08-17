
#include <esp_now.h>
#include <WiFi.h>
#include "key.h"

#define RELAY_PIN 23
#define RELAY_ON_TIME 5000 // 3 secondi

typedef struct struct_message {
  bool switchState;
} struct_message;

struct_message incomingMsg;
struct_message ackMsg;

unsigned long relayStartTime = 0;
bool relayActive = false;

// MAC del master

// Mac address Sender A2  44:1D:64:F4:8C:F8
uint8_t masterAddress[] = {0x44, 0x1D, 0x64, 0xF4, 0x8C, 0xF8};

void onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  // Se riceve LOW dal master e il relè non è già attivo
  if (incomingMsg.switchState && !relayActive) {
    digitalWrite(RELAY_PIN, HIGH);
    relayStartTime = millis();
    relayActive = true;

    // Invia ACK
    ackMsg.switchState = true;
    esp_now_send(masterAddress, (uint8_t *)&ackMsg, sizeof(ackMsg));
    Serial.println("Relè acceso, ACK inviato");
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address (STA): ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = true;
  memcpy(peerInfo.lmk, ESP_NOW_KEY, 16);

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add master peer");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);
}

void loop() {
  // Spegni il relè dopo RELAY_ON_TIME senza resettare il timer
  if (relayActive && millis() - relayStartTime >= RELAY_ON_TIME) {
    digitalWrite(RELAY_PIN, LOW);
    relayActive = false;
    Serial.println("Relè spento dopo 3 secondi");
  }
}
