#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// =================== CONFIG DISPLAY ===================
TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

// =================== PULSANTI ===================
#define BTN_UP     32
#define BTN_DOWN   33
#define BTN_LEFT   25
#define BTN_RIGHT  26

// Stato dei pulsanti
bool btnState[4] = {false, false, false, false};
unsigned long lastDebounceTime[4] = {0,0,0,0};
const unsigned long debounceDelay = 50;

// =================== FLUSH DISPLAY ===================
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w*h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// =================== FUNZIONE INGRESSO PULSANTI ===================
bool read_button(uint8_t pin, uint8_t index) {
    bool reading = digitalRead(pin);
    if (reading != btnState[index]) {
        lastDebounceTime[index] = millis();
    }
    if ((millis() - lastDebounceTime[index]) > debounceDelay) {
        btnState[index] = reading;
    }
    return btnState[index];
}

// =================== MENU LVGL ===================
lv_obj_t *menu_label;
int menu_index = 0;
const char* menu_items[] = {"Voce 1", "Voce 2", "Voce 3", "Voce 4"};
const int menu_count = 4;

void update_menu() {
    String text = "";
    for(int i=0; i<menu_count; i++) {
        if(i == menu_index) text += "> "; 
        else text += "  ";
        text += menu_items[i];
        text += "\n";
    }
    lv_label_set_text(menu_label, text.c_str());
}

// =================== SETUP LVGL ===================
void setup_lvgl() {
    lv_init();
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    lv_disp_drv_register(&disp_drv);

    // Creazione label per menu
    menu_label = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(menu_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(menu_label, 200);
    lv_obj_center(menu_label);
    update_menu();
}

// =================== ARDUINO SETUP ===================
void setup() {
    Serial.begin(115200);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    setup_lvgl();
}

// =================== LOOP ===================
void loop() {
    lv_timer_handler();

    if(read_button(BTN_UP, 0)) {
        menu_index--;
        if(menu_index < 0) menu_index = menu_count - 1;
        update_menu();
        delay(150); // anti-ripetizione veloce
    }

    if(read_button(BTN_DOWN, 1)) {
        menu_index++;
        if(menu_index >= menu_count) menu_index = 0;
        update_menu();
        delay(150);
    }

    if(read_button(BTN_LEFT, 2)) {
        Serial.println("LEFT pressed");
        delay(150);
    }

    if(read_button(BTN_RIGHT, 3)) {
        Serial.println("RIGHT pressed");
        delay(150);
    }

    delay(5);
}
