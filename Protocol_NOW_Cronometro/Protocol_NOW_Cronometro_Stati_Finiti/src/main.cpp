
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "pin_config.h"
#include <esp_adc_cal.h>
#include <esp_now.h>
#include <WiFi.h>
#include "key.h"
#include <Wire.h>

// === PIN CONFIG ===
#define BTN_START  18
#define BTN_STOP   17
#define BTN_RESET  43
#define BTN_CLEAR  44  
#define BUZZER     16
#define DURATA_BUZZER 800
#define DURATA_BUZZER_STOP 1000
//#define RELAY_PIN 2
#define CRONO_STOP_ON_TIME 3000 // 3 secondi

esp_adc_cal_characteristics_t adc_chars;

typedef struct struct_message {
  bool switchState;
} struct_message;

struct_message incomingMsg;
struct_message ackMsg;

unsigned long relayStartTime = 0;
bool cronoActive = false;

// Mac address Sender  88:57:21:6F:B3:D8
uint8_t masterAddress[] = {0x88, 0x57, 0x21, 0x6F, 0xB3, 0xD8};

void onDataReceived(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  // Se riceve LOW dal master Cronometro non è già attivo
  if (incomingMsg.switchState && !cronoActive) {
    //digitalWrite(RELAY_PIN, HIGH);
    //relayStartTime = millis();
    cronoActive = true;

    // Invia ACK
    ackMsg.switchState = true;
    esp_now_send(masterAddress, (uint8_t *)&ackMsg, sizeof(ackMsg));
    Serial.println("Relè acceso, ACK inviato");
  }
}


// Strutture che conterra i dati del cronometro
typedef struct {
  int minuti;
  int secondi;
  int decimi;
}Cronometro;

Cronometro cronometro;


// === Oggetti Display ===
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite header = TFT_eSprite(&tft); 
TFT_eSprite body   = TFT_eSprite(&tft);

// === Stati ===
enum State { WAIT_START, COUNTDOWN, BUZZER_ON, RUNNING, STOPPED, FINISHED };
State currentState = WAIT_START;

// === Variabili ===
unsigned long countdownStart = 0;
unsigned long buzzerStart = 0;
unsigned long chronoStart = 0;
unsigned long elapsedWhenStopped = 0;
int countdownValue = 3;
int lastTenths = -1;
volatile unsigned int battFiltered = 0;

// === Pulsanti ===
bool isButtonPressed(int pin) {
  static unsigned long lastPress[10] = {0};
  if (digitalRead(pin) == LOW) {
    if (millis() - lastPress[pin] > 200) {
      lastPress[pin] = millis();
      return true;
    }
  }
  return false;
}

// Calcolo percentuale batteria
int batteriaPercentuale(int v) {
    if (v >= 4200) return 100;
    else if (v >= 4100) return 90 + (v - 4100) / 10;   // 4100-4200 → 90-100%
    else if (v >= 4000) return 80 + (v - 4000) / 10;   // 4000-4100 → 80-90%
    else if (v >= 3900) return 60 + (v - 3900) / 20;   // 3900-4000 → 60-80%
    else if (v >= 3800) return 40 + (v - 3800) / 10;   // 3800-3900 → 40-60%
    else if (v >= 3700) return 20 + (v - 3700) / 10;   // 3700-3800 → 20-40%
    else if (v >= 3600) return 10 + (v - 3600) / 10;   // 3600-3700 → 10-20%
    else if (v >= 3500) return 5  + (v - 3500) / 20;   // 3500-3600 → 5-10%
    else if (v >= 3400) return 2  + (v - 3400) / 50;   // 3400-3500 → 2-5%
    else if (v >= 3300) return 1;                      // 3300-3400 → 1-2%
    else return 0;                                     // sotto i 3.3V → scarica
}


// Legge la tensione batteria con multisampling + calibrazione + filtro IIR
unsigned int batteria() {
    const int samples = 16;       // numero di campioni da mediare
    uint32_t sum = 0;

    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_BAT_VOLT);
    }
    uint32_t raw_avg = sum / samples;   // media campioni

    // Converte in millivolt con calibrazione esp_adc_cal
    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw_avg, &adc_chars) * 2;

    // Filtro IIR
    static float filtro = 0;
    const float alpha = 0.1;  // più basso = più lento e stabile
    filtro = alpha * voltage + (1.0 - alpha) * filtro;

    return (unsigned int)filtro;
}


// Funzione draw body con cerchio
void drawBody(const char* text, int textSize = 4, uint16_t color = TFT_GREEN) {
    body.fillSprite(TFT_BLACK);
    int16_t cx = body.width()/2;
    int16_t cy = body.height()/2;
    int radius = 50;
    body.drawCircle(cx, cy, radius, TFT_DARKGREY);
    body.fillCircle(cx, cy, radius-2, TFT_BLACK);

    body.setTextSize(textSize);
    body.setTextDatum(MC_DATUM);
    body.setTextColor(TFT_BLACK, TFT_BLACK);
    body.drawString(text, cx+2, cy+2);

    body.setTextColor(color, TFT_BLACK);
    body.drawString(text, cx, cy);

    for(int i=0;i<12;i++){
        float angle = i*30*3.1415926/180.0;
        int x0 = cx + cos(angle)*(radius+2);
        int y0 = cy + sin(angle)*(radius+2);
        int x1 = cx + cos(angle)*(radius+6);
        int y1 = cy + sin(angle)*(radius+6);
        body.drawLine(x0,y0,x1,y1,color);
    }
    body.pushSprite(0,30);
}


// Funzione reset cronometro
void resetCronometro() {
    cronometro.minuti = 0;
    cronometro.secondi = 0;
    cronometro.decimi  = 0;
    elapsedWhenStopped = 0;
    chronoStart = millis();
    lastTenths = -1;
    drawBody("00:00.0", 4, TFT_GREEN);
}



// Draw header con batteria filtrata
void drawHeader(const char* leftText, uint16_t color=TFT_WHITE) {
    header.fillSprite(TFT_BLACK);
    header.setTextDatum(MC_DATUM);
    header.setTextSize(2);
    header.setTextColor(TFT_YELLOW, TFT_BLACK);
    header.drawString(leftText, 60, 20);

    // Leggiamo la tensione filtrata
    unsigned int batt = batteria();
    header.drawString(String(batt)+"mV", 200, 20);
    //header.drawString(String(isCharging()), 160, 20);
    header.drawString(String(batteriaPercentuale(battFiltered))+"%", 300, 20);
    header.pushSprite(0,0);
}


// Draw countdown con arco fluido, 0 e GO correttamente
void drawCountdownSpicchi(unsigned long elapsedMs, int countdownSec) {
    body.fillSprite(TFT_BLACK);

    int centerX = body.width() / 2;
    int centerY = body.height() / 2;
    int radius = 50;

    // Frazione di tempo rimasta
    float fraction = 1.0 - (float)elapsedMs / (countdownSec * 1000.0);
    if (fraction < 0) fraction = 0;

    // Angolo dell'arco in gradi
    float angleArc = 360.0 * fraction;

    // Disegna l'arco come linee sottili
    for (float angle = -90; angle < -90 + angleArc; angle += 1.0) {
        float rad = angle * PI / 180.0;
        int x = centerX + cos(rad) * radius;
        int y = centerY + sin(rad) * radius;
        body.drawLine(centerX, centerY, x, y, TFT_RED);
    }

    // Cerchio esterno
    body.drawCircle(centerX, centerY, radius, TFT_WHITE);

    // Testo centrale
    body.setTextDatum(MC_DATUM);
    body.setTextSize(6);
    body.setTextColor(TFT_YELLOW, TFT_BLACK);

    char buf[4];
    if (elapsedMs >= countdownSec * 1000) {
        sprintf(buf, "GO"); // tempo scaduto
    } else {
        int secondsLeft = (int)floor(countdownSec * fraction); // floor per mostrare 0 nell'ultimo secondo
        sprintf(buf, "%d", secondsLeft);
    }

    body.drawString(buf, centerX, centerY);

    body.pushSprite(0, 30);
}


void TaskBatteria(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        const int samples = 16;  // numero di campioni da mediare
        uint32_t sum = 0;

        for (int i = 0; i < samples; i++) {
            sum += analogRead(PIN_BAT_VOLT);
        }
        uint32_t raw_avg = sum / samples;

        // Converte in millivolt con calibrazione esp_adc_cal
        uint32_t voltage = esp_adc_cal_raw_to_voltage(raw_avg, &adc_chars) * 2;

        // Filtro IIR
        static float filtro = 0;
        const float alpha = 0.1;
        filtro = alpha * voltage + (1.0 - alpha) * filtro;

        battFiltered = (unsigned int)filtro;

        vTaskDelay(pdMS_TO_TICKS(200));  
    }
}


void setup() {
  Serial.begin(9600);
  /* pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  */

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_STOP, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_CLEAR, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  header.createSprite(320,30);
  body.createSprite(320,140);
  drawBody("Premi START",3,TFT_GREEN);

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

  xTaskCreatePinnedToCore(
    TaskBatteria,    // funzione
    "TaskBatteria",  // nome
    2048,            // stack
    NULL,            // parametri
    1,               // priorità
    NULL,            // handle
    1                // core (1 = secondario su ESP32)
);

}


void loop() {

    unsigned long now=millis();
    //unsigned int batt = batteria();
     //Serial.println(WiFi.macAddress());

    switch(currentState){
        case WAIT_START:
            drawHeader("Wait",TFT_WHITE);
            if(isButtonPressed(BTN_START)){
                countdownStart=now;
                countdownValue=3;
                drawHeader("Countdown",TFT_CYAN);
                drawCountdownSpicchi(0,3);
                currentState=COUNTDOWN;
            }
            break;

        case COUNTDOWN:
            {
                unsigned long elapsed=now-countdownStart;
                static unsigned long lastDraw=0;
                if(elapsed - lastDraw >=50){
                    lastDraw=elapsed;
                    drawCountdownSpicchi(elapsed,3);
                }
                if(elapsed>=3000){
                    buzzerStart=now;
                    digitalWrite(BUZZER,HIGH);
                    currentState=BUZZER_ON;
                }
            }
            break;

        case BUZZER_ON:
            if(now-buzzerStart>=DURATA_BUZZER){
                digitalWrite(BUZZER,LOW);
                chronoStart=now;
                elapsedWhenStopped=0;
                lastTenths=-1;
                //drawHeader("Fire",TFT_WHITE);
                currentState=RUNNING;
            }
            break;

        case RUNNING:
            {
                //bool stopReceived = isButtonPressed(BTN_STOP) || (incomingMsg.switchState);
                drawHeader("FIRE", TFT_YELLOW);
                unsigned long elapsed = now - chronoStart;
                unsigned long total = elapsedWhenStopped + elapsed;

                cronometro.minuti = total / 60000;
                cronometro.secondi = (total % 60000) / 1000;
                cronometro.decimi  = (total % 1000) / 10;

                if(cronometro.decimi != lastTenths){
                    lastTenths=cronometro.decimi;
                    char buf[16];
                    sprintf(buf,"%02d:%02d:%02d",cronometro.minuti,cronometro.secondi,cronometro.decimi);
                    drawBody(buf,4,TFT_CYAN);
                }

                if (incomingMsg.switchState && cronoActive) {
                      buzzerStart = now;
                      digitalWrite(BUZZER, HIGH);
                      elapsedWhenStopped += elapsed;
                      //drawHeader("STOPPED", TFT_YELLOW);
                      currentState = STOPPED;

                  } else if (isButtonPressed(BTN_STOP)) {
                      buzzerStart = now;
                      digitalWrite(BUZZER, HIGH);
                      elapsedWhenStopped += elapsed;
                      //drawHeader("STOPPED", TFT_YELLOW);
                      currentState = STOPPED;
                  }


                // AZZERAMENTO cronometro con quarto tasto
                if(isButtonPressed(BTN_CLEAR)){
                    resetCronometro();
                    currentState=FINISHED;
                }

                /* // Esempio: entra in FINISHED dopo 5 minuti
                if(total >= 5*60*1000){
                    digitalWrite(BUZZER,HIGH);
                    currentState=FINISHED;
                } */
            }
            break;

        case STOPPED:
            drawHeader("STOPPED", TFT_YELLOW);
            if(now-buzzerStart>=DURATA_BUZZER_STOP) digitalWrite(BUZZER,LOW);

            if(isButtonPressed(BTN_RESET)){
                drawHeader("Cronometro",TFT_WHITE);
                drawBody("Premi START",3,TFT_GREEN);
                currentState=WAIT_START;
            }

            if(isButtonPressed(BTN_CLEAR)){
                resetCronometro();
                currentState=FINISHED;
            }
            break;

        case FINISHED:
            drawHeader("FINITO",TFT_RED);
            drawBody("00:00.00",4,TFT_CYAN);
            incomingMsg.switchState = ! incomingMsg.switchState;

            if(isButtonPressed(BTN_RESET)){
                drawHeader("Cronometro",TFT_WHITE);
                drawBody("Premi START",3,TFT_GREEN);
                currentState=WAIT_START;
            }

            if(isButtonPressed(BTN_CLEAR)){
                resetCronometro();
            }
            break;
    }



   // Spegne  CRONO_STOP_ON_TIME senza resettare il timer
  if (cronoActive && millis() - relayStartTime >= CRONO_STOP_ON_TIME) {
    //digitalWrite(RELAY_PIN, LOW);
    cronoActive = false;
    Serial.println("Relè spento dopo 3 secondi");
  } 
}