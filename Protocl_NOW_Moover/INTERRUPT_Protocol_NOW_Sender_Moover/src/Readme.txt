EC:E3:34:14:7C:88


#include <WiFi.h>
#include <WiFiUdp.h>

const char* ssid = "PepperOscill";
const char* password = "12345678999";

WiFiUDP udp;
// const char* udpBroadcast = "255.255.255.255";
const int udpPort = 4210;


// Impostazioni IP statico AP
IPAddress localIP(192, 168, 4, 1);    // IP ESP32 come AP
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// IP client autorizzato
IPAddress allowedClientIP(192, 168, 4, 2);

#define BUTTON_PIN 23

volatile bool buttonEvent = false; // Segnale ISR
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ISR rapida per il pulsante
void IRAM_ATTR handleButtonChange() {
  buttonEvent = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Avvio Access Point senza isolamento client
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 1, false);
  Serial.print("AP avviato: ");
  Serial.println(ssid);
  Serial.print("IP AP: ");
  Serial.println(WiFi.softAPIP());

  udp.begin(udpPort);

  // Attiva interrupt sul pulsante
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonChange, CHANGE);
}

void loop() {
  // --- Gestione pulsante con debounce ---
  if (buttonEvent) {
    buttonEvent = false;
    bool reading = digitalRead(BUTTON_PIN);
    unsigned long now = millis();
    if (reading != lastButtonState && (now - lastDebounceTime) > debounceDelay) {
      lastButtonState = reading;
      lastDebounceTime = now;

      if (reading == LOW) {
        udp.beginPacket(allowedClientIP, udpPort);
        udp.print("RELAY_ON");
        udp.endPacket();
        Serial.println("Comando inviato: RELAY_ON");
      } else {
        udp.beginPacket(allowedClientIP, udpPort);
        udp.print("RELAY_OFF");
        udp.endPacket();
        Serial.println("Comando inviato: RELAY_OFF");
      }
    }
  }

  // --- Ricezione ACK ---
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char incomingPacket[255];
    int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
    if (len > 0) incomingPacket[len] = '\0';

    if (strcmp(incomingPacket, "RELAY_ON_ACK") == 0) {
      Serial.println("[ACK] Relè acceso");
    } else if (strcmp(incomingPacket, "RELAY_OFF_ACK") == 0) {
      Serial.println("[ACK] Relè spento manualmente");
    } else if (strcmp(incomingPacket, "RELAY_AUTO_OFF_ACK") == 0) {
      Serial.println("[ACK] Relè spento automaticamente dopo 3 secondi");
    } else {
      Serial.printf("[ACK] Messaggio sconosciuto: %s\n", incomingPacket);
    }
  }
}
