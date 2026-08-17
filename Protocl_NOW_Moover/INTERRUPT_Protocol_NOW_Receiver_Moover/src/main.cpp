#include <esp_now.h>
#include <WiFi.h>
#include "key.h"
#include "mac.h"

// Mac address Sender A2  EC:E3:34:14:7C:88
///uint8_t masterAddress[] = {0xEC, 0xE3, 0x34, 0x14, 0x7C, 0x88};

#define RELAY_PIN 23
#define RELAY_ON_TIME 5000 // 5 secondi

typedef struct struct_message {
  bool switchState;
} struct_message;

struct_message incomingMsg;
struct_message ackMsg;

unsigned long relayStartTime = 0;
bool relayActive = false;

void onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  if (incomingMsg.switchState) {
    digitalWrite(RELAY_PIN, HIGH);
    relayStartTime = millis(); // reset timer se già attivo
    relayActive = true;

    ackMsg.switchState = true;
    esp_err_t result = esp_now_send(masterAddress, (uint8_t *)&ackMsg, sizeof(ackMsg));
    if (result == ESP_OK) Serial.println("ACK inviato correttamente");
    else Serial.println("Errore invio ACK");
    
    Serial.println("Relè acceso / timer aggiornato");
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
  if (relayActive && millis() - relayStartTime >= RELAY_ON_TIME) {
    digitalWrite(RELAY_PIN, LOW);
    relayActive = false;
    Serial.println("Relè spento dopo 5 secondi");
  }
}
