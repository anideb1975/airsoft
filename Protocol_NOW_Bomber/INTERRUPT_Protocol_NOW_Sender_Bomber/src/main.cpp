
#include <esp_now.h>
#include <WiFi.h>
#include "key.h"
#include "mac.h"

#define SWITCH_PIN 23
#define DEBOUNCE_DELAY 50
#define ACK_TIMEOUT 1000 // Timeout 1 secondo

typedef struct struct_message {
  bool switchState;
} struct_message;

struct_message msg;
struct_message incomingAck;

volatile bool switchPressedFlag = false;
unsigned long lastInterruptTime = 0;

unsigned long lastSentTime = 0;
bool waitingForAck = false;

// MAC address dello slave
//uint8_t slaveAddress[] = {0x80, 0xF3, 0xDA, 0x62, 0xD3, 0x78};

// Callback per ricezione dati
void onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingAck, incomingData, sizeof(incomingAck));
  if (incomingAck.switchState) {
    Serial.println("ACK ricevuto dallo slave!");
    waitingForAck = false; // Reset flag ACK
  }
}

// Funzione per inviare messaggio
void sendMessage() {
  esp_now_send(slaveAddress, (uint8_t *)&msg, sizeof(msg));
  lastSentTime = millis();
  waitingForAck = true;
  Serial.println("Messaggio inviato allo slave");
}

// Interrupt handler
void IRAM_ATTR switchISR() {
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime > DEBOUNCE_DELAY) {
    switchPressedFlag = true;
    lastInterruptTime = currentTime;
  }
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

  // Configuro interrupt sul pin
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), switchISR, FALLING);
}

void loop() {
  // Gestione pressione tramite interrupt
  if (switchPressedFlag && !waitingForAck) {
    msg.switchState = true;
    sendMessage();
    switchPressedFlag = false;
  }

  // Timeout ACK
  if (waitingForAck && millis() - lastSentTime >= ACK_TIMEOUT) {
    Serial.println("ACK non ricevuto: reinvio messaggio");
    sendMessage();
  }
}
