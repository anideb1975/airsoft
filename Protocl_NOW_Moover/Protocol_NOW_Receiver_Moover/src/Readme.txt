EC:E3:34:14:9F:BC



#include <WiFi.h>
#include <WiFiUdp.h>

const char* ssid = "PepperOscill";
const char* password = "12345678999";

WiFiUDP udp;
const int udpPort = 4210;

// Impostazioni IP statico
IPAddress localIP(192, 168, 4, 2);      // IP ESP32
IPAddress gateway(192, 168, 4, 1);       // Gateway
IPAddress subnet(255, 255, 255, 0);      // Subnet



#define RELAY_PIN 23  // Pin relè

bool relayOn = false;
unsigned long relayStartTime = 0;
const unsigned long relayDuration = 3000; // 3 secondi

// Ultimo client che ha acceso il relè
IPAddress lastClientIP;
uint16_t lastClientPort;

// ---------------- Helper ----------------
void logMsg(const char* msg) {
    Serial.printf("[%lu] %s\n", millis(), msg);
}

void sendAck(IPAddress clientIP, uint16_t clientPort, const char* message) {
    udp.beginPacket(clientIP, clientPort);
    udp.print(message);
    udp.endPacket();
}

// ---------------- Controllo relè ----------------
void turnRelayOn(IPAddress clientIP, uint16_t clientPort) {
    digitalWrite(RELAY_PIN, HIGH);
    relayOn = true;
    relayStartTime = millis();

    // Salvo client per ACK automatico
    lastClientIP = clientIP;
    lastClientPort = clientPort;

    logMsg("Relè ON");
    sendAck(clientIP, clientPort, "RELAY_ON_ACK");
}

void turnRelayOff(IPAddress clientIP, uint16_t clientPort, const char* reason) {
    digitalWrite(RELAY_PIN, LOW);
    relayOn = false;

    Serial.printf("[%lu] Relè OFF (%s)\n", millis(), reason);

    if (strcmp(reason, "manual") == 0) {
        sendAck(clientIP, clientPort, "RELAY_OFF_ACK");
    } else {
        sendAck(clientIP, clientPort, "RELAY_AUTO_OFF_ACK");
    }
}

// ---------------- Setup ----------------
void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Solo STA o AP + STA se necessario
    WiFi.mode(WIFI_STA);  
    WiFi.begin(ssid, password);

    Serial.printf("Connessione a %s", ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnesso!");
    Serial.print("IP ricevuto: ");
    Serial.println(WiFi.localIP());

    udp.begin(udpPort);
    Serial.printf("UDP in ascolto sulla porta %d\n", udpPort);
}

// ---------------- Loop ----------------
void loop() {
    // Ricezione pacchetti UDP
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char incomingPacket[64];  // buffer sicuro
        int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
        if (len > 0) incomingPacket[len] = '\0';

        Serial.printf("[%lu] Ricevuto: %s da %s:%d\n", millis(), incomingPacket,
                      udp.remoteIP().toString().c_str(), udp.remotePort());

        if (strcmp(incomingPacket, "RELAY_ON") == 0) {
            turnRelayOn(udp.remoteIP(), udp.remotePort());
        }
        else if (strcmp(incomingPacket, "RELAY_OFF") == 0) {
            if (!relayOn) {
                turnRelayOff(udp.remoteIP(), udp.remotePort(), "manual");
            } else {
                logMsg("Ignoro RELAY_OFF: relè in auto-timer");
            }
        }
        else {
            Serial.printf("[%lu] Comando sconosciuto: %s\n", millis(), incomingPacket);
        }
    }

    // Spegnimento automatico dopo relayDuration
    if (relayOn && (millis() - relayStartTime >= relayDuration)) {
        turnRelayOff(lastClientIP, lastClientPort, "auto");
    }
}
