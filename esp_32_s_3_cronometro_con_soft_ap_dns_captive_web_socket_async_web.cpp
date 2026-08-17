#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "pin_config.h"
#include <esp_adc_cal.h>

// === Nuove dipendenze per Web asincrono / DNS / WebSocket ===
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

/*
 * Librerie richieste (Library Manager):
 * - ESP Async WebServer (me-no-dev)
 * - AsyncTCP
 * - DNSServer (inclusa con ESP32 core)
 */

// =================== BATTERIA ===================
esp_adc_cal_characteristics_t adc_chars;

// =================== WIFI / AP ===================
const char* ssid     = "softAirAP";
const char* password = "12345678999";

// IP AP statico
IPAddress local_IP(192,168,4,1);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);

// DNS server per captive portal
DNSServer dnsServer;              // Risponde a qualunque dominio con 192.168.4.1

// Web server asincrono + WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// =================== UDP (compatibilità) ===================
WiFiUDP udp;                      // Per STOP/RESET/START via UDP se serve
const int udpPort = 4210;

// =================== DISPLAY ===================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite header = TFT_eSprite(&tft);
TFT_eSprite body   = TFT_eSprite(&tft);

// =================== PULSANTI / BUZZER ===================
#define BTN_START  18
#define BTN_STOP   17
#define BTN_RESET  43
#define BUZZER     16

// Debounce sicuro per pin specifici
bool isButtonPressed(int pin) {
  static unsigned long lastPressStart = 0;
  static unsigned long lastPressStop  = 0;
  static unsigned long lastPressReset = 0;
  unsigned long* lastPress = nullptr;

  switch (pin) {
    case BTN_START: lastPress = &lastPressStart; break;
    case BTN_STOP:  lastPress = &lastPressStop;  break;
    case BTN_RESET: lastPress = &lastPressReset; break;
    default: return false;
  }

  if (digitalRead(pin) == LOW) {
    if (millis() - *lastPress > 200) {
      *lastPress = millis();
      return true;
    }
  }
  return false;
}

// =================== STATI ===================
enum State { WAIT_START, COUNTDOWN, BUZZER_ON, RUNNING, STOPPED };
State currentState = WAIT_START;

// Flag: consenti RESET anche da UDP/WebSocket
bool allowUdpReset = true;

// Struttura cronometro
typedef struct {
  int minuti;
  int secondi;
  int decimi;   // 0..9
} Cronometro;
Cronometro cronometro;

// Variabili tempo/stato
unsigned long countdownStart = 0;
unsigned long buzzerStart    = 0;
unsigned long chronoStart    = 0;
unsigned long elapsedWhenStopped = 0;
int countdownValue = 3;
int lastTenths     = -1;

// Comandi UDP/WS
enum UdpCommand { UDP_NONE, UDP_STOP, UDP_RESET, UDP_START, UDP_TOGGLE_RESET_ON, UDP_TOGGLE_RESET_OFF };

// =================== UTILITY BATTERIA ===================
int batteriaPercentuale(int mv) {
  if (mv >= 4200) return 100;
  if (mv <= 3200) return 0;
  return (int)(((float)(mv - 3200) / (4200 - 3200)) * 100.0f);
}

unsigned int batteria() {
  uint32_t raw = analogRead(PIN_BAT_VOLT);
  uint32_t mv  = esp_adc_cal_raw_to_voltage(raw, &adc_chars) * 2; // partitore 1:2
  return mv; // mV
}

// =================== UI ===================
void drawHeader(const char* leftText, unsigned int batt = 0 , uint16_t color = TFT_WHITE) {
  header.fillSprite(TFT_BLACK);
  header.setTextDatum(MC_DATUM);
  header.setTextSize(2);

  header.setTextColor(TFT_YELLOW, TFT_BLACK);
  header.drawString(leftText, 70, 20);

  if (batt > 4300) {
    header.setTextColor(TFT_RED, TFT_BLACK);
    header.drawString("In Carica", 250, 20);
  } else {
    header.setTextColor(TFT_RED, TFT_BLACK);
    String s = String(batteriaPercentuale(batt)) + "%";
    header.drawString(s, 260, 20);
  }
  header.pushSprite(0, 0);
}

void drawBody(const char* text, int textSize = 4, uint16_t color = TFT_GREEN) {
  body.fillSprite(TFT_BLACK);
  body.setTextSize(textSize);
  body.setTextColor(color, TFT_BLACK);
  int16_t x = (body.width() - body.textWidth(text)) / 2;
  int16_t y = (body.height() - (textSize * 8)) / 2;
  body.setCursor(x, y);
  body.println(text);
  body.pushSprite(0, 30);
}

// =================== UDP ===================
UdpCommand udpCommandReceived() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buf[64];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) buf[len] = '\0';
    String cmd = String(buf);
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "STOP")            return UDP_STOP;
    if (cmd == "RESET")           return UDP_RESET;
    if (cmd == "START")           return UDP_START;
    if (cmd == "ENABLE_RESET")    return UDP_TOGGLE_RESET_ON;
    if (cmd == "DISABLE_RESET")   return UDP_TOGGLE_RESET_OFF;
  }
  return UDP_NONE;
}

// =================== WEBSOCKET ===================
String jsonStatus(unsigned int battMv) {
  // Costruisce un JSON leggero con stato corrente
  // {"state":"RUNNING","time":"mm:ss.d","batt":4020,"allowUdpReset":1}
  char tbuf[20];
  sprintf(tbuf, "%02d:%02d.%01d", cronometro.minuti, cronometro.secondi, cronometro.decimi);

  String s = "{";
  s += "\"state\":\"";
  switch (currentState) {
    case WAIT_START: s += "WAIT_START"; break;
    case COUNTDOWN:  s += "COUNTDOWN";  break;
    case BUZZER_ON:  s += "BUZZER_ON";  break;
    case RUNNING:    s += "RUNNING";    break;
    case STOPPED:    s += "STOPPED";    break;
  }
  s += "\",\"time\":\""; s += tbuf; s += "\"";
  s += ",\"batt\":"; s += battMv;
  s += ",\"allowUdpReset\":"; s += (allowUdpReset ? 1 : 0);
  s += "}";
  return s;
}

void broadcastStatus(unsigned int battMv) {
  String payload = jsonStatus(battMv);
  ws.textAll(payload);
}

void handleWsEvent(AsyncWebSocket       *server,
                   AsyncWebSocketClient *client,
                   AwsEventType          type,
                   void                 *arg,
                   uint8_t              *data,
                   size_t                len) {
  if (type == WS_EVT_CONNECT) {
    // Appena un client si collega, inviagli subito lo stato
    unsigned int batt = batteria();
    client->text(jsonStatus(batt));
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      String msg = String((const char*)data, len);
      msg.trim(); msg.toUpperCase();

      if (msg == "START") {
        if (currentState == WAIT_START || currentState == STOPPED) {
          countdownStart = millis();
          countdownValue = 3;
          drawHeader("Countdown", batteria(), TFT_CYAN);
          drawBody("3", 6, TFT_RED);
          currentState = COUNTDOWN;
        }
      } else if (msg == "STOP") {
        if (currentState == RUNNING) {
          unsigned long now = millis();
          unsigned long elapsed = now - chronoStart;
          elapsedWhenStopped += elapsed;
          drawHeader("STOPPED", batteria(), TFT_YELLOW);
          currentState = STOPPED;
        }
      } else if (msg == "RESET") {
        if (allowUdpReset || isButtonPressed(9999)) { /* segnaposto */ }
        if (allowUdpReset && (currentState == STOPPED || currentState == WAIT_START)) {
          elapsedWhenStopped = 0;
          drawHeader("Cronometro", batteria(), TFT_WHITE);
          drawBody("Premi START", 3, TFT_GREEN);
          currentState = WAIT_START;
          header.fillSprite(TFT_BLACK);
          header.pushSprite(0,0);
        }
      } else if (msg == "ENABLE_RESET") {
        allowUdpReset = true;
      } else if (msg == "DISABLE_RESET") {
        allowUdpReset = false;
      }
    }
  }
}

// =================== PAGINA WEB (captive) ===================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Cronometro</title>
  <style>
    html,body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu; background:#0b0b0b; color:#e5e5e5; margin:0;}
    .wrap{max-width:680px; margin:0 auto; padding:24px;}
    .card{background:#141414; border-radius:16px; padding:20px; box-shadow:0 8px 24px rgba(0,0,0,.35);}
    .timer{font-size:56px; text-align:center; letter-spacing:1px;}
    .state{opacity:.8; text-align:center; margin-top:6px;}
    .row{display:flex; gap:12px; margin-top:16px; flex-wrap:wrap;}
    button{flex:1; padding:14px 16px; border:0; border-radius:12px; font-size:16px; cursor:pointer;}
    .start{background:#2e7d32; color:#fff;}
    .stop{background:#c62828; color:#fff;}
    .reset{background:#1565c0; color:#fff;}
    .badge{display:inline-block; background:#222; padding:6px 10px; border-radius:10px; font-size:13px;}
    .muted{color:#bbb}
  </style>
</head>
<body>
  <div class="wrap">
    <h2>Cronometro <span id="batt" class="badge">batt: --</span> <span id="flag" class="badge">udp reset: ?</span></h2>
    <div class="card">
      <div id="timer" class="timer">00:00.0</div>
      <div id="state" class="state muted">WAIT_START</div>
      <div class="row">
        <button class="start" onclick="send('START')">START</button>
        <button class="stop"  onclick="send('STOP')">STOP</button>
        <button class="reset" onclick="send('RESET')">RESET</button>
      </div>
      <div class="row">
        <button onclick="send('ENABLE_RESET')">Abilita RESET remoto</button>
        <button onclick="send('DISABLE_RESET')">Disabilita RESET remoto</button>
      </div>
    </div>
    <p class="muted">Se non vedi aggiornamenti, controlla che il telefono sia connesso alla Wi‑Fi <b>softAirAP</b>.</p>
  </div>
  <script>
    let ws;
    function connect(){
      const proto = location.protocol === 'https:' ? 'wss' : 'ws';
      ws = new WebSocket(`${proto}://${location.host}/ws`);
      ws.onopen = () => console.log('WS connesso');
      ws.onmessage = (ev) => {
        try{
          const j = JSON.parse(ev.data);
          if (j.time)  document.getElementById('timer').textContent = j.time;
          if (j.state) document.getElementById('state').textContent = j.state;
          if (typeof j.batt !== 'undefined') document.getElementById('batt').textContent = `batt: ${j.batt} mV`;
          if (typeof j.allowUdpReset !== 'undefined') document.getElementById('flag').textContent = `udp reset: ${j.allowUdpReset? 'ON':'OFF'}`;
        }catch(e){console.log(e);}
      };
      ws.onclose = () => setTimeout(connect, 1000);
    }
    function send(cmd){ if (ws && ws.readyState===1) ws.send(cmd); }
    connect();
  </script>
</body>
</html>
)HTML";

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_STOP,  INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  // Necessario per lettura batteria su alcune board T-Display S3
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  // Caratterizzazione ADC
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  (void)val_type;

  // TFT
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  header.createSprite(320, 30);
  body.createSprite(320, 140);
  drawBody("Premi START", 3);

  // Configurazione AP
  if(!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("[AP] Errore configurazione IP SoftAP");
  }
  bool result = WiFi.softAP(ssid, password, 2);
  if(result){
    Serial.println("[AP] SoftAP attivo!");
    Serial.print("IP SoftAP: "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[AP] Errore nell'avvio SoftAP");
  }

  // DNS captive: rispondi con 192.168.4.1 a ogni dominio
  dnsServer.start(53, "*", local_IP);

  // UDP
  udp.begin(udpPort);

  // WebSocket
  ws.onEvent(handleWsEvent);
  server.addHandler(&ws);

  // Route principali (cattura tutto -> index)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });
  server.onNotFound([](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", INDEX_HTML);
  });

  // Avvio server
  server.begin();
}

// =================== LOOP ===================
void loop() {
  // DNS deve essere processato nel loop
  dnsServer.processNextRequest();

  unsigned long now = millis();
  unsigned int batt = batteria();

  switch (currentState) {
    case WAIT_START:
      if (isButtonPressed(BTN_START)) {
        countdownStart = now;
        countdownValue = 3;
        drawHeader("Countdown", batt, TFT_CYAN);
        drawBody("3", 6, TFT_RED);
        currentState = COUNTDOWN;
      }
      break;

    case COUNTDOWN:
      if (now - countdownStart >= 1000) {
        countdownStart = now;
        countdownValue--;
        if (countdownValue > 0) {
          char buf[3];
          sprintf(buf, "%d", countdownValue);
          drawBody(buf, 6, TFT_RED);
        } else {
          drawBody("GO", 6, TFT_YELLOW);
          buzzerStart = now;
          digitalWrite(BUZZER, HIGH);
          currentState = BUZZER_ON;
        }
      }
      break;

    case BUZZER_ON:
      if (now - buzzerStart >= 1000) {
        digitalWrite(BUZZER, LOW);
        chronoStart = now;
        elapsedWhenStopped = 0;
        lastTenths = -1;
        drawHeader("Cronometro", batt, TFT_WHITE);
        currentState = RUNNING;
      }
      break;

    case RUNNING: {
      unsigned long elapsed = now - chronoStart;
      unsigned long total   = elapsedWhenStopped + elapsed;

      cronometro.minuti  = total / 60000;
      cronometro.secondi = (total % 60000) / 1000;
      cronometro.decimi  = (total % 1000) / 100; // 0..9

      if (cronometro.decimi != lastTenths) {
        lastTenths = cronometro.decimi;
        char buf[16];
        sprintf(buf, "%02d:%02d.%01d", cronometro.minuti, cronometro.secondi, cronometro.decimi);
        drawBody(buf, 4, TFT_CYAN);

        // Aggiorna i client WebSocket ~10Hz
        broadcastStatus(batt);
      }

      // Comandi da UDP
      UdpCommand u = udpCommandReceived();
      if (u == UDP_TOGGLE_RESET_ON)  allowUdpReset = true;
      if (u == UDP_TOGGLE_RESET_OFF) allowUdpReset = false;

      if (isButtonPressed(BTN_STOP) || u == UDP_STOP) {
        elapsedWhenStopped += elapsed;
        drawHeader("STOPPED", batt, TFT_YELLOW);
        currentState = STOPPED;
      }
    } break;

    case STOPPED: {
      // Invia aggiornamenti periodici anche in STOP (circa ogni 200ms)
      static unsigned long lastPush = 0;
      if (now - lastPush > 200) { lastPush = now; broadcastStatus(batt); }

      UdpCommand u = udpCommandReceived();
      if (u == UDP_TOGGLE_RESET_ON)  allowUdpReset = true;
      if (u == UDP_TOGGLE_RESET_OFF) allowUdpReset = false;

      if (isButtonPressed(BTN_RESET) || (allowUdpReset && u == UDP_RESET)) {
        elapsedWhenStopped = 0;
        drawHeader("Cronometro", batt, TFT_WHITE);
        drawBody("Premi START", 3, TFT_GREEN);
        currentState = WAIT_START;
        header.fillSprite(TFT_BLACK);
        header.pushSprite(0,0);
      }
    } break;
  }

  // Aggiorna stato ai client anche quando si è in attesa (ogni 500ms)
  static unsigned long lastIdlePush = 0;
  if ((currentState == WAIT_START || currentState == COUNTDOWN || currentState == BUZZER_ON)) {
    if (now - lastIdlePush > 500) {
      lastIdlePush = now;
      // tempo visualizzato: quello calcolato l'ultima volta
      broadcastStatus(batt);
    }
  }
}

// ====== Verifiche di setup TFT (come da tuo codice) ======
#if PIN_LCD_WR  != TFT_WR || \
    PIN_LCD_RD  != TFT_RD || \
    PIN_LCD_CS  != TFT_CS || \
    PIN_LCD_DC  != TFT_DC || \
    PIN_LCD_RES != TFT_RST|| \
    PIN_LCD_D0  != TFT_D0 || \
    PIN_LCD_D1  != TFT_D1 || \
    PIN_LCD_D2  != TFT_D2 || \
    PIN_LCD_D3  != TFT_D3 || \
    PIN_LCD_D4  != TFT_D4 || \
    PIN_LCD_D5  != TFT_D5 || \
    PIN_LCD_D6  != TFT_D6 || \
    PIN_LCD_D7  != TFT_D7 || \
    PIN_LCD_BL  != TFT_BL || \
    TFT_BACKLIGHT_ON != HIGH || \
    170   != TFT_WIDTH  || \
    320   != TFT_HEIGHT
#error  "Error! Please make sure <User_Setups/Setup206_LilyGo_T_Display_S3.h> is selected in <TFT_eSPI/User_Setup_Select.h>"
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#error  "The current version is not supported for the time being, please use a version below Arduino ESP32 3.0"
#endif
