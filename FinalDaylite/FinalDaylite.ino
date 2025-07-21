#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// Weather icons (scaled to 80% size)
#include "sunny.h"
#include "clear_night.h"
#include "mostly_sunny.h"
#include "mostly_clear_night.h"
#include "partly_cloudy.h"
#include "partly_cloudy_night.h"
#include "cloudy.h"
#include "haze_fog_dust_smoke.h"
#include "drizzle.h"
#include "showers_rain.h"
#include "heavy_rain.h"
#include "scattered_showers_day.h"
#include "scattered_showers_night.h"
#include "heavy_snow.h"
#include "snow_showers_snow.h"
#include "isolated_scattered_tstorms_day.h"
#include "isolated_scattered_tstorms_night.h"

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
#define LCD_BACKLIGHT_PIN 21
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Buffer size calculations
#define BUF_ROWS 42
#define DRAW_BUF_SIZE (SCREEN_WIDTH * BUF_ROWS)

TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;

// Theme Colors
#define THEME_COUNT 2
uint32_t theme_bg_top[THEME_COUNT]    = {0xA8FFE3, 0x2c3e50}; // Light teal, Dark blue
uint32_t theme_bg_bottom[THEME_COUNT] = {0x56E3C3, 0x1a2530}; // Dark teal, Darker blue
uint32_t theme_card[THEME_COUNT]      = {0x4FD7B7, 0x34495e}; // Card color
uint32_t theme_accent[THEME_COUNT]    = {0x223a5c, 0xecf0f1}; // Text color
uint32_t theme_separator[THEME_COUNT] = {0xc0c0c0, 0x606060}; // Separator colors
#define SEPARATOR_OPA  0x22

#define BTN_DARK_BG     0x223a5c
#define BTN_DARK_TEXT   0xffffff
#define BTN_RED         0xFF0000
#define BTN_GREEN       0x00AA00

#define CARD_TEXT       0xf5f6fa
#define MENU_BG         0xeeeeee
#define MENU_GRAD_TOP   0xeeeeee
#define MENU_GRAD_BOTTOM 0xeeeeee
#define RAIN_TEXT       0x88aaff

String cur_city = "London";
String latitude = "51.5074";
String longitude = "-0.1278";
bool use_24h = true, use_fahrenheit = false, night_mode = false, dark_theme = false;
int brightness = 255;
bool menu_open = false, city_select_open = false;
bool pending_weather_update = false;
unsigned long last_weather_update = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000UL; // 10 minutes
const int NIGHT_MODE_BRIGHTNESS = 20;
const int DEFAULT_BRIGHTNESS = 180;

// UI Elements
static lv_obj_t *scr_home, *menu_panel, *city_page;
static lv_obj_t *lbl_time, *lbl_temp, *lbl_desc, *lbl_rain;
static lv_obj_t *box_hourly, *box_weekly;
static lv_obj_t *hourly_bar[4][3];
static lv_obj_t *weekly_row[4][5];
static lv_obj_t *slider_brightness, *switch_24h, *switch_f, *switch_night, *switch_theme, *btn_wifi, *btn_city;
static lv_obj_t *city_ta = nullptr, *city_kb = nullptr;
static lv_obj_t *img_weather = nullptr;

// City search globals
static DynamicJsonDocument geoDoc(8 * 1024);
static JsonArray geoResults;
static char dd_opts[512];
static lv_obj_t *results_dd = nullptr;  // Dropdown for city results

// Forward Declarations
void open_menu();
void close_menu();
void update_time(lv_timer_t*);
void fetch_and_update_weather();
String urlencode(const String &str);
void create_settings_window();
const char* translate(const char* english);
void update_ui_language();
void do_geocode_query(const char *q);
void update_brightness();
void show_splash();
void apply_theme();
void set_dark_theme(bool enabled);
void update_menu_theme();
void open_city_page();

void validate_image(const lv_img_dsc_t* img, const char* name) {
    if (!img) {
        Serial.printf("[IMAGE] %s: NULL pointer\n", name);
        return;
    }
    Serial.printf("[IMAGE] %s validation:\n", name);
    Serial.printf("  w: %d, h: %d\n", img->header.w, img->header.h);
    Serial.printf("  cf: %d\n", img->header.cf);
    Serial.printf("  data_size: %d\n", img->data_size);
    uint32_t expected_size = img->header.w * img->header.h * 2;
    if (img->data_size != expected_size) {
        Serial.printf("  ERROR: Size mismatch! Expected: %d, Actual: %d\n", expected_size, img->data_size);
    } else {
        Serial.println("  Size OK");
    }
}

const lv_img_dsc_t* get_weather_image(int code, int is_day) {
    const lv_img_dsc_t* img = nullptr;
    switch(code) {
        case 0:
            img = is_day ? &sunny : &clear_night;
            break;
        case 1:
            img = is_day ? &mostly_sunny : &mostly_clear_night;
            break;
        case 2:
            img = is_day ? &partly_cloudy : &partly_cloudy_night;
            break;
        case 3:
        case 4:
            img = &cloudy;
            break;
        case 45:
        case 48:
            img = &haze_fog_dust_smoke;
            break;
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            img = &drizzle;
            break;
        case 61:
        case 63:
        case 66:
            img = &showers_rain;
            break;
        case 65:
        case 67:
        case 82:
            img = &heavy_rain;
            break;
        case 80:
        case 81:
            img = is_day ? &scattered_showers_day : &scattered_showers_night;
            break;
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
            img = &snow_showers_snow;
            break;
        case 86:
            img = &heavy_snow;
            break;
        case 95:
        case 96:
        case 99:
            img = is_day ? &isolated_scattered_tstorms_day : &isolated_scattered_tstorms_night;
            break;
        default:
            if (is_day) {
                if (code > 80) img = &heavy_rain;
                else if (code > 70) img = &snow_showers_snow;
                else img = &partly_cloudy;
            } else {
                if (code > 80) img = &heavy_rain;
                else if (code > 70) img = &snow_showers_snow;
                else img = &partly_cloudy_night;
            }
    }

    if (img) {
        static bool validated = false;
        if (!validated) {
            validate_image(img, "weather_icon");
            validated = true;
        }
    } else {
        Serial.println("[IMAGE] No image selected!");
    }
    return img;
}

void update_menu_theme() {
    if (!menu_panel) return;
    uint32_t text_color = dark_theme ? 0xFFFFFF : 0x223a5c;
    uint32_t sep_color = dark_theme ? 0x606060 : 0xc0c0c0;

    uint32_t child_cnt = lv_obj_get_child_cnt(menu_panel);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(menu_panel, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_color(child, lv_color_hex(text_color), 0);
        }
        else {
            lv_coord_t w = lv_obj_get_width(child);
            lv_coord_t h = lv_obj_get_height(child);
            if (h == 1 && w == SCREEN_WIDTH-44) {
                lv_obj_set_style_bg_color(child, lv_color_hex(sep_color), 0);
            }
        }
    }
}

void apply_theme() {
    int theme_idx = dark_theme ? 1 : 0;
    
    if (scr_home) {
        lv_obj_set_style_bg_color(scr_home, lv_color_hex(theme_bg_top[theme_idx]), 0);
        lv_obj_set_style_bg_grad_dir(scr_home, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_width(scr_home, 0, 0);
        lv_obj_set_scrollbar_mode(scr_home, LV_SCROLLBAR_MODE_OFF);
    }
    if (box_hourly) {
        lv_obj_set_style_bg_color(box_hourly, lv_color_hex(theme_card[theme_idx]), 0);
        lv_obj_set_style_border_width(box_hourly, 0, 0);
        lv_obj_set_style_radius(box_hourly, 14, 0);
        lv_obj_set_scrollbar_mode(box_hourly, LV_SCROLLBAR_MODE_OFF);
    }
    if (box_weekly) {
        lv_obj_set_style_bg_color(box_weekly, lv_color_hex(theme_card[theme_idx]), 0);
        lv_obj_set_style_border_width(box_weekly, 0, 0);
        lv_obj_set_style_radius(box_weekly, 14, 0);
        lv_obj_set_scrollbar_mode(box_weekly, LV_SCROLLBAR_MODE_OFF);
    }
    if (menu_panel) {
        lv_obj_set_style_bg_color(menu_panel, lv_color_hex(dark_theme ? 0x333333 : MENU_BG), 0);
        lv_obj_set_style_bg_grad_dir(menu_panel, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_width(menu_panel, 0, 0);
        lv_obj_move_foreground(menu_panel);
        lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (city_page) {
        lv_obj_set_style_bg_color(city_page, lv_color_hex(dark_theme ? 0x333333 : MENU_GRAD_TOP), 0);
        lv_obj_set_style_bg_grad_dir(city_page, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_width(city_page, 0, 0);
        lv_obj_move_foreground(city_page);
        lv_obj_clear_flag(city_page, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (lbl_temp) lv_obj_set_style_text_color(lbl_temp, lv_color_hex(theme_accent[theme_idx]), 0);
    if (lbl_desc) lv_obj_set_style_text_color(lbl_desc, lv_color_hex(theme_accent[theme_idx]), 0);
    if (lbl_rain) lv_obj_set_style_text_color(lbl_rain, lv_color_hex(RAIN_TEXT), 0);
    if (btn_city) {
        lv_obj_set_style_bg_color(btn_city, lv_color_hex(BTN_DARK_BG), 0);
    }
    if (btn_wifi) {
        lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(BTN_RED), 0);
    }
    if (lbl_time) {
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(dark_theme ? 0xecf0f1 : 0x223a5c), 0);
    }
    
    // Update hourly and weekly text colors
    for (int i = 0; i < 4; i++) {
        if (hourly_bar[i][0]) {
            lv_obj_set_style_text_color(hourly_bar[i][0], lv_color_hex(CARD_TEXT), 0);
        }
        if (hourly_bar[i][1]) {
            lv_obj_set_style_text_color(hourly_bar[i][1], lv_color_hex(theme_accent[theme_idx]), 0);
        }
        if (i < 3 && hourly_bar[i][2]) {
            lv_obj_set_style_bg_color(hourly_bar[i][2], lv_color_hex(theme_separator[theme_idx]), 0);
        }
    }
    
    for (int i = 0; i < 4; i++) {
        if (weekly_row[i][0]) {
            lv_obj_set_style_text_color(weekly_row[i][0], lv_color_hex(CARD_TEXT), 0);
        }
        if (weekly_row[i][2]) {
            lv_obj_set_style_text_color(weekly_row[i][2], lv_color_hex(theme_accent[theme_idx]), 0);
        }
        if (weekly_row[i][3]) {
            lv_obj_set_style_text_color(weekly_row[i][3], lv_color_hex(theme_accent[theme_idx]), 0);
        }
        if (i < 3 && weekly_row[i][4]) {
            lv_obj_set_style_bg_color(weekly_row[i][4], lv_color_hex(theme_separator[theme_idx]), 0);
        }
    }
    
    // Update menu theme if open
    if (menu_open) {
        update_menu_theme();
    }
}

// UPDATED TOUCHSCREEN FUNCTION FOR LVGL v9
static void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();
        int x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
        int y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
        x = constrain(x, 0, SCREEN_WIDTH-1);
        y = constrain(y, 0, SCREEN_HEIGHT-1);
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

String urlencode(const String &str) {
  String encoded = "";
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

void populate_results_dropdown() {
    if (!results_dd) return;
    dd_opts[0] = '\0';  // Clear the buffer
    for (JsonObject item : geoResults) {
        strcat(dd_opts, item["name"].as<const char*>());
        if (item["admin1"]) {
            strcat(dd_opts, ", ");
            strcat(dd_opts, item["admin1"].as<const char*>());
        }
        strcat(dd_opts, "\n");
    }
    if (geoResults.size() > 0) {
        lv_dropdown_set_options_static(results_dd, dd_opts);
        lv_obj_clear_flag(results_dd, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);
    }
}

void do_geocode_query(const char *q) {
    geoDoc.clear();
    String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") + 
                urlencode(q) + "&count=10";
    HTTPClient http;
    http.begin(url);
    if (http.GET() == HTTP_CODE_OK) {
        auto err = deserializeJson(geoDoc, http.getString());
        if (!err) {
            geoResults = geoDoc["results"].as<JsonArray>();
            populate_results_dropdown();
        }
    }
    http.end();
}

static void location_save_event_cb(lv_event_t *e) {
    if (geoResults.size() == 0) {
        Serial.println("No geo results available");
        return;
    }
    uint16_t idx = lv_dropdown_get_selected(results_dd);
    if (idx >= geoResults.size()) {
        Serial.printf("Invalid index: %d (max: %d)\n", idx, geoResults.size()-1);
        return;
    }
    JsonObject item = geoResults[idx];
    double lat = item["latitude"].as<double>();
    double lon = item["longitude"].as<double>();
    char lat_buf[16], lon_buf[16];
    snprintf(lat_buf, sizeof(lat_buf), "%.6f", lat);
    snprintf(lon_buf, sizeof(lon_buf), "%.6f", lon);
    latitude = lat_buf;
    longitude = lon_buf;
    cur_city = item["name"].as<String>();
    Serial.printf("Saving location: %s (%.6f, %.6f)\n", cur_city.c_str(), lat, lon);
    prefs.putString("latitude", latitude);
    prefs.putString("longitude", longitude);
    prefs.putString("location", cur_city);
    if (city_page) { 
        lv_obj_del(city_page); 
        city_page = nullptr; 
    }
    city_select_open = false;
    pending_weather_update = true;
}

static void swipe_area_event_cb(lv_event_t *e) {
    static int start_y = 0;
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t p; 
        lv_indev_get_point(indev, &p);
        start_y = p.y;
    } else if(code == LV_EVENT_RELEASED) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t p; 
        lv_indev_get_point(indev, &p);
        int drag_dist_y = p.y - start_y;
        if(drag_dist_y > 60 && !menu_open && !city_select_open) {
            open_menu();
        }
    }
}

static void menu_swipe_area_event_cb(lv_event_t *e) {
    static int start_y = 0;
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t p; 
        lv_indev_get_point(indev, &p);
        start_y = p.y;
    } else if(code == LV_EVENT_RELEASED) {
        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t p; 
        lv_indev_get_point(indev, &p);
        int drag_dist_y = p.y - start_y;
        if(drag_dist_y < -60 && menu_open && !city_select_open) {
            close_menu();
        }
    }
}

// --- Translation function replaced, always returns English ---
const char* translate(const char* english) {
    return english;
}

void update_ui_language() {
    // All UI is in English, just refresh values
    if (lbl_desc) lv_label_set_text(lbl_desc, "--");
    if (lbl_rain) lv_label_set_text(lbl_rain, "--");
    pending_weather_update = true;
}

void open_menu() {
    if(menu_panel) return;
    menu_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(menu_panel, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(menu_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(menu_panel, lv_color_hex(dark_theme ? 0x333333 : MENU_BG), 0);
    lv_obj_set_style_bg_grad_dir(menu_panel, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(menu_panel, 0, 0);
    lv_obj_set_style_radius(menu_panel, 0, 0);
    lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *menu_swipe_top = lv_obj_create(menu_panel);
    lv_obj_set_size(menu_swipe_top, SCREEN_WIDTH, 44);
    lv_obj_align(menu_swipe_top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu_swipe_top, LV_OPA_0, 0);
    lv_obj_add_flag(menu_swipe_top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(menu_swipe_top, menu_swipe_area_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_width(menu_swipe_top, 0, 0);

    lv_obj_t *menu_swipe_bottom = lv_obj_create(menu_panel);
    lv_obj_set_size(menu_swipe_bottom, SCREEN_WIDTH, 60);
    lv_obj_align(menu_swipe_bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu_swipe_bottom, LV_OPA_0, 0);
    lv_obj_add_flag(menu_swipe_bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(menu_swipe_bottom, menu_swipe_area_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_width(menu_swipe_bottom, 0, 0);

    lv_obj_add_event_cb(menu_panel, menu_swipe_area_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t* lbl = lv_label_create(menu_panel);
    lv_label_set_text(lbl, "Daylite User Menu");
    lv_obj_set_style_text_color(lbl, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 18);

    int y = 50;
    int row_spacing = 36;

    // Row 1: 24h Time
    lv_obj_t* lbl_timef = lv_label_create(menu_panel);
    lv_label_set_text(lbl_timef, "24h Time");
    lv_obj_set_style_text_font(lbl_timef, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_timef, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_align(lbl_timef, LV_ALIGN_TOP_LEFT, 28, y);

    switch_24h = lv_switch_create(menu_panel);
    lv_obj_align(switch_24h, LV_ALIGN_TOP_RIGHT, -22, y-2);
    if(use_24h) lv_obj_add_state(switch_24h, LV_STATE_CHECKED);
    else lv_obj_clear_state(switch_24h, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_24h, [](lv_event_t *e){ 
        use_24h = lv_obj_has_state(switch_24h, LV_STATE_CHECKED);
        prefs.putBool("use24Hour", use_24h);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Row 2: Fahrenheit
    lv_obj_t* lbl_f = lv_label_create(menu_panel);
    lv_label_set_text(lbl_f, "Fahrenheit");
    lv_obj_set_style_text_font(lbl_f, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_f, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_align(lbl_f, LV_ALIGN_TOP_LEFT, 28, y + row_spacing);

    switch_f = lv_switch_create(menu_panel);
    lv_obj_align(switch_f, LV_ALIGN_TOP_RIGHT, -22, y + row_spacing - 2);
    if(use_fahrenheit) lv_obj_add_state(switch_f, LV_STATE_CHECKED);
    else lv_obj_clear_state(switch_f, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_f, [](lv_event_t *e) {
        use_fahrenheit = lv_obj_has_state(switch_f, LV_STATE_CHECKED);
        prefs.putBool("useFahrenheit", use_fahrenheit);
        pending_weather_update = true;
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Row 3: Dark Theme
    lv_obj_t* lbl_theme = lv_label_create(menu_panel);
    lv_label_set_text(lbl_theme, "Dark Theme");
    lv_obj_set_style_text_font(lbl_theme, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_theme, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_align(lbl_theme, LV_ALIGN_TOP_LEFT, 28, y + 2*row_spacing);

    switch_theme = lv_switch_create(menu_panel);
    lv_obj_align(switch_theme, LV_ALIGN_TOP_RIGHT, -22, y + 2*row_spacing - 2);
    if(dark_theme) lv_obj_add_state(switch_theme, LV_STATE_CHECKED);
    else lv_obj_clear_state(switch_theme, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_theme, [](lv_event_t *e) {
        dark_theme = lv_obj_has_state(switch_theme, LV_STATE_CHECKED);
        prefs.putBool("dark_theme", dark_theme);
        set_dark_theme(dark_theme);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Row 4: Night Mode
    lv_obj_t* lbl_night = lv_label_create(menu_panel);
    lv_label_set_text(lbl_night, "Night Mode");
    lv_obj_set_style_text_font(lbl_night, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_night, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_align(lbl_night, LV_ALIGN_TOP_LEFT, 28, y + 3*row_spacing);

    switch_night = lv_switch_create(menu_panel);
    lv_obj_align(switch_night, LV_ALIGN_TOP_RIGHT, -22, y + 3*row_spacing - 2);
    if(night_mode) lv_obj_add_state(switch_night, LV_STATE_CHECKED);
    else lv_obj_clear_state(switch_night, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_night, [](lv_event_t *e) {
        night_mode = lv_obj_has_state(switch_night, LV_STATE_CHECKED);
        prefs.putBool("night_mode", night_mode);
        update_brightness();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* sep1 = lv_obj_create(menu_panel);
    lv_obj_set_size(sep1, SCREEN_WIDTH-44, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(dark_theme ? 0x606060 : 0xc0c0c0), 0);
    lv_obj_set_style_bg_opa(sep1, SEPARATOR_OPA, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_align(sep1, LV_ALIGN_TOP_MID, 0, y + 4*row_spacing - 10);

    // Brightness section
    lv_obj_t* lbl_bright = lv_label_create(menu_panel);
    lv_label_set_text(lbl_bright, "Brightness");
    lv_obj_set_style_text_color(lbl_bright, lv_color_hex(dark_theme ? 0xffffff : 0x234), 0);
    lv_obj_set_style_text_font(lbl_bright, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_bright, LV_ALIGN_TOP_LEFT, 28, y + 4*row_spacing);

    slider_brightness = lv_slider_create(menu_panel);
    lv_slider_set_range(slider_brightness, 10, 255);
    lv_slider_set_value(slider_brightness, brightness, LV_ANIM_OFF);
    lv_obj_set_width(slider_brightness, SCREEN_WIDTH-90);
    lv_obj_align(slider_brightness, LV_ALIGN_TOP_MID, 0, y + 4*row_spacing + 30);
    lv_obj_add_event_cb(slider_brightness, [](lv_event_t *e){
        brightness = lv_slider_get_value(slider_brightness);
        prefs.putUInt("brightness", brightness);
        update_brightness();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* sep2 = lv_obj_create(menu_panel);
    lv_obj_set_size(sep2, SCREEN_WIDTH-44, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(dark_theme ? 0x606060 : 0xc0c0c0), 0);
    lv_obj_set_style_bg_opa(sep2, SEPARATOR_OPA, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_align(sep2, LV_ALIGN_TOP_MID, 0, y + 4*row_spacing + 60);

    // City button - full width
    btn_city = lv_btn_create(menu_panel);
    lv_obj_set_size(btn_city, SCREEN_WIDTH-44, 36);
    lv_obj_set_style_bg_color(btn_city, lv_color_hex(BTN_DARK_BG), 0);
    lv_obj_set_style_radius(btn_city, 12, 0);
    lv_obj_set_style_border_width(btn_city, 0, 0);
    lv_obj_align(btn_city, LV_ALIGN_TOP_MID, 0, y + 4*row_spacing + 70);
    lv_obj_add_event_cb(btn_city, [](lv_event_t* e){ 
        close_menu();
        delay(100);
        city_select_open = false;
        city_page = nullptr;
        open_city_page();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_city_btn = lv_label_create(btn_city);
    lv_label_set_text_fmt(lbl_city_btn, "Change City  |  %s", cur_city.c_str());
    lv_obj_set_style_text_color(lbl_city_btn, lv_color_hex(BTN_DARK_TEXT), 0);
    lv_obj_center(lbl_city_btn);

    // WiFi button - full width
    btn_wifi = lv_btn_create(menu_panel);
    lv_obj_set_size(btn_wifi, SCREEN_WIDTH-44, 36);
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(BTN_RED), 0);
    lv_obj_set_style_radius(btn_wifi, 12, 0);
    lv_obj_set_style_border_width(btn_wifi, 0, 0);
    lv_obj_align(btn_wifi, LV_ALIGN_TOP_MID, 0, y + 4*row_spacing + 116);
    lv_obj_add_event_cb(btn_wifi, [](lv_event_t *e){
        lv_obj_t *confirm_popup = lv_obj_create(lv_scr_act());
        lv_obj_set_size(confirm_popup, 200, 150);
        lv_obj_center(confirm_popup);
        lv_obj_set_style_bg_color(confirm_popup, lv_color_hex(dark_theme ? 0x444444 : 0xFFFFFF), 0);
        lv_obj_set_style_radius(confirm_popup, 10, 0);
        lv_obj_set_style_border_width(confirm_popup, 0, 0);
        lv_obj_t *lbl = lv_label_create(confirm_popup);
        lv_label_set_text(lbl, "Reset WiFi?");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(dark_theme ? 0xffffff : 0x000000), 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_t *btn_yes = lv_btn_create(confirm_popup);
        lv_obj_set_size(btn_yes, 80, 40);
        lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_set_style_bg_color(btn_yes, lv_color_hex(BTN_RED), 0);
        lv_obj_t *lbl_yes = lv_label_create(btn_yes);
        lv_label_set_text(lbl_yes, "Yes");
        lv_obj_set_style_text_color(lbl_yes, lv_color_hex(BTN_DARK_TEXT), 0);
        lv_obj_center(lbl_yes);
        lv_obj_add_event_cb(btn_yes, [](lv_event_t *e){
            WiFiManager wm;
            wm.resetSettings();
            delay(1500);
            ESP.restart();
        }, LV_EVENT_CLICKED, NULL);
        lv_obj_t *btn_no = lv_btn_create(confirm_popup);
        lv_obj_set_size(btn_no, 80, 40);
        lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        lv_obj_set_style_bg_color(btn_no, lv_color_hex(BTN_GREEN), 0);
        lv_obj_t *lbl_no = lv_label_create(btn_no);
        lv_label_set_text(lbl_no, "No");
        lv_obj_set_style_text_color(lbl_no, lv_color_hex(BTN_DARK_TEXT), 0);
        lv_obj_center(lbl_no);
        lv_obj_add_event_cb(btn_no, [](lv_event_t *e){
            lv_obj_del(static_cast<lv_obj_t*>(lv_event_get_user_data(e)));
        }, LV_EVENT_CLICKED, confirm_popup);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, "Reset WiFi");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(BTN_DARK_TEXT), 0);
    lv_obj_center(lbl_wifi);

    menu_open = true;
    apply_theme();
}

void close_menu() {
    if (!menu_panel) return;
    lv_obj_del(menu_panel);
    menu_panel = NULL;
    menu_open = false;
}

void open_city_page() {
    if(city_page) return;
    city_select_open = true;
    city_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(city_page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(city_page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(city_page, lv_color_hex(dark_theme ? 0x333333 : MENU_GRAD_TOP), 0);
    lv_obj_set_style_bg_grad_dir(city_page, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(city_page, 0, 0);
    lv_obj_clear_flag(city_page, LV_OBJ_FLAG_SCROLLABLE);

    // Close button
    lv_obj_t *btn_close = lv_btn_create(city_page);
    lv_obj_set_size(btn_close, 34, 34);
    lv_obj_align(btn_close, LV_ALIGN_TOP_LEFT, 4, 8);
    lv_obj_set_style_radius(btn_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
    lv_obj_set_style_border_width(btn_close, 0, 0);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lbl_close, lv_color_hex(0xffffff), 0);
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, [](lv_event_t* e){ 
        if(city_page) { 
            lv_obj_del(city_page); 
            city_page=nullptr; 
            city_select_open=false;
        } 
    }, LV_EVENT_CLICKED, NULL);

    // Title
    lv_obj_t *lbl = lv_label_create(city_page);
    lv_label_set_text(lbl, "Choose City");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(dark_theme ? 0xffffff : 0x000000), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 18, 20);

    // City search box
    city_ta = lv_textarea_create(city_page);
    lv_obj_set_width(city_ta, SCREEN_WIDTH-50);
    lv_obj_set_height(city_ta, 30);
    lv_obj_align(city_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_placeholder_text(city_ta, "Search city...");
    lv_textarea_set_one_line(city_ta, true);

    // Results dropdown
    results_dd = lv_dropdown_create(city_page);
    lv_obj_set_width(results_dd, SCREEN_WIDTH-50);
    lv_obj_align(results_dd, LV_ALIGN_TOP_MID, 0, 100);
    lv_dropdown_set_options(results_dd, "");
    lv_obj_add_flag(results_dd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(results_dd, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
            location_save_event_cb(e);
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Keyboard
    city_kb = lv_keyboard_create(city_page);
    lv_keyboard_set_textarea(city_kb, city_ta);
    lv_obj_set_size(city_kb, SCREEN_WIDTH-10, 120);
    lv_obj_align(city_kb, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_border_width(city_kb, 0, 0);
    lv_obj_add_event_cb(city_kb, [](lv_event_t *e){
        lv_event_code_t code = lv_event_get_code(e);
        if(code == LV_EVENT_READY) {
            const char *txt = lv_textarea_get_text(city_ta);
            if (strlen(txt) > 0) {
                do_geocode_query(txt);
            }
        }
    }, LV_EVENT_ALL, NULL);
}

void setup_home_screen() {
    scr_home = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(scr_home, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(scr_home, 0, 0);
    lv_scr_load(scr_home);
    apply_theme();

    // Swipe area at top
    lv_obj_t *swipe_area = lv_obj_create(scr_home);
    lv_obj_set_size(swipe_area, SCREEN_WIDTH, 40);
    lv_obj_align(swipe_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(swipe_area, LV_OPA_0, 0);
    lv_obj_add_flag(swipe_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(swipe_area, swipe_area_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_border_width(swipe_area, 0, 0);

    // Time label
    lbl_time = lv_label_create(scr_home);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(dark_theme ? 0xecf0f1 : 0x223a5c), 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_label_set_text(lbl_time, "--:--");

    // Weather description
    lbl_desc = lv_label_create(scr_home);
    lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
    lv_obj_align(lbl_desc, LV_ALIGN_TOP_LEFT, 14, 16);
    lv_label_set_text(lbl_desc, "--");

    // Weather icon (scaled to 80% size)
    img_weather = lv_img_create(scr_home);
    lv_img_set_src(img_weather, &sunny);
    lv_img_set_zoom(img_weather, 204);  // 204/256 = 80% scale
    lv_obj_set_size(img_weather, 80, 80);
    lv_obj_align(img_weather, LV_ALIGN_TOP_RIGHT, -10, 40);
    lv_obj_clear_flag(img_weather, LV_OBJ_FLAG_HIDDEN);

    // Temperature
    lbl_temp = lv_label_create(scr_home);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_42, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
    lv_obj_align(lbl_temp, LV_ALIGN_TOP_LEFT, 14, 45);
    lv_label_set_text(lbl_temp, "--°C");

    // Rain probability
    lbl_rain = lv_label_create(scr_home);
    lv_obj_set_style_text_font(lbl_rain, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_rain, lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(lbl_rain, "--");
    lv_obj_set_width(lbl_rain, 160);
    lv_label_set_long_mode(lbl_rain, LV_LABEL_LONG_WRAP);

    // Hourly forecast
    int hbar_y = 160;
    int hbar_h = 38;
    int hour_col_w = (SCREEN_WIDTH-20-16*2) / 4;
    box_hourly = lv_obj_create(scr_home);
    lv_obj_set_size(box_hourly, SCREEN_WIDTH-20, hbar_h);
    lv_obj_align(box_hourly, LV_ALIGN_TOP_MID, 0, hbar_y);
    lv_obj_set_style_bg_color(box_hourly, lv_color_hex(theme_card[dark_theme ? 1 : 0]), 0);
    lv_obj_set_style_radius(box_hourly, 14, 0);
    lv_obj_set_style_pad_all(box_hourly, 0, 0);
    lv_obj_set_style_border_width(box_hourly, 0, 0);
    lv_obj_set_scrollbar_mode(box_hourly, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < 4; i++) {
        hourly_bar[i][0] = lv_label_create(box_hourly);
        lv_obj_set_style_text_font(hourly_bar[i][0], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hourly_bar[i][0], lv_color_hex(CARD_TEXT), 0);
        lv_obj_set_width(hourly_bar[i][0], hour_col_w);
        lv_obj_align(hourly_bar[i][0], LV_ALIGN_TOP_LEFT, 16 + i*hour_col_w, 2);

        hourly_bar[i][1] = lv_label_create(box_hourly);
        lv_obj_set_style_text_font(hourly_bar[i][1], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hourly_bar[i][1], lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
        lv_obj_set_width(hourly_bar[i][1], hour_col_w);
        lv_obj_align(hourly_bar[i][1], LV_ALIGN_BOTTOM_LEFT, 16 + i*hour_col_w, -2);

        if(i!=3) {
            hourly_bar[i][2] = lv_obj_create(box_hourly);
            lv_obj_set_style_bg_color(hourly_bar[i][2], lv_color_hex(theme_separator[dark_theme ? 1 : 0]), 0);
            lv_obj_set_style_bg_opa(hourly_bar[i][2], SEPARATOR_OPA, 0);
            lv_obj_set_size(hourly_bar[i][2], 1, hbar_h-8);
            lv_obj_align(hourly_bar[i][2], LV_ALIGN_TOP_LEFT, 16 + (i+1)*hour_col_w-5, 6);
            lv_obj_clear_flag(hourly_bar[i][2], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(hourly_bar[i][2], 0, 0);
        }
    }

    // Weekly forecast
    int weekly_rows = 4;
    int row_h = 22;
    int week_box_height = row_h * weekly_rows + 14;
    box_weekly = lv_obj_create(scr_home);
    lv_obj_set_size(box_weekly, SCREEN_WIDTH-20, week_box_height);
    lv_obj_align(box_weekly, LV_ALIGN_TOP_MID, 0, hbar_y + hbar_h + 8);
    lv_obj_set_style_bg_color(box_weekly, lv_color_hex(theme_card[dark_theme ? 1 : 0]), 0);
    lv_obj_set_style_radius(box_weekly, 14, 0);
    lv_obj_set_style_border_width(box_weekly, 0, 0);
    lv_obj_set_style_pad_all(box_weekly, 9, 0);
    lv_obj_set_scrollbar_mode(box_weekly, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(box_weekly, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < weekly_rows; i++) {
        weekly_row[i][1] = lv_img_create(box_weekly);
        lv_img_set_src(weekly_row[i][1], &drizzle);
        lv_obj_set_size(weekly_row[i][1], 20, 20);
        lv_img_set_zoom(weekly_row[i][1], 51);
        lv_obj_align(weekly_row[i][1], LV_ALIGN_TOP_LEFT, 2, i*row_h + 1);
        lv_obj_add_flag(weekly_row[i][1], LV_OBJ_FLAG_HIDDEN);

        weekly_row[i][0] = lv_label_create(box_weekly);
        lv_obj_set_style_text_font(weekly_row[i][0], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(weekly_row[i][0], lv_color_hex(CARD_TEXT), 0);
        lv_obj_align_to(weekly_row[i][0], weekly_row[i][1], LV_ALIGN_OUT_RIGHT_MID, 8, 0);

        weekly_row[i][2] = lv_label_create(box_weekly);
        lv_obj_set_style_text_font(weekly_row[i][2], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(weekly_row[i][2], lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
        lv_obj_align(weekly_row[i][2], LV_ALIGN_TOP_RIGHT, -6, i*row_h);

        weekly_row[i][3] = lv_label_create(box_weekly);
        lv_obj_set_style_text_font(weekly_row[i][3], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(weekly_row[i][3], lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);
        lv_obj_align(weekly_row[i][3], LV_ALIGN_TOP_RIGHT, -46, i*row_h);

        if(i != weekly_rows-1) {
            weekly_row[i][4] = lv_obj_create(box_weekly);
            lv_obj_set_style_bg_color(weekly_row[i][4], lv_color_hex(theme_separator[dark_theme ? 1 : 0]), 0);
            lv_obj_set_style_bg_opa(weekly_row[i][4], SEPARATOR_OPA, 0);
            lv_obj_set_size(weekly_row[i][4], SCREEN_WIDTH-38, 1);
            lv_obj_align(weekly_row[i][4], LV_ALIGN_TOP_LEFT, 0, (i+1)*row_h-6);
            lv_obj_clear_flag(weekly_row[i][4], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(weekly_row[i][4], 0, 0);
        }
    }
    apply_theme();
}

void update_time(lv_timer_t*) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        lv_label_set_text(lbl_time, "--:--");
        return;
    }
    char buf[16];
    if (use_24h) {
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        int hour = timeinfo.tm_hour % 12;
        if (hour == 0) hour = 12;
        const char *ampm = (timeinfo.tm_hour < 12) ? "am" : "pm";
        snprintf(buf, sizeof(buf), "%d:%02d%s", hour, timeinfo.tm_min, ampm);
    }
    lv_label_set_text(lbl_time, buf);
}

void fetch_and_update_weather() {
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + latitude +
                 "&longitude=" + longitude +
                 "&current=temperature_2m,weathercode,precipitation,is_day" +
                 "&hourly=temperature_2m,precipitation_probability" +
                 "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
                 "&forecast_days=4&timezone=auto";
    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) { 
        http.end(); 
        lv_label_set_text(lbl_desc, "API error"); 
        return; 
    }
    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(16384);
    auto err = deserializeJson(doc, payload);
    if (err) { 
        lv_label_set_text(lbl_desc, "API error"); 
        return; 
    }

    int utc_offset_seconds = doc["utc_offset_seconds"].as<int>();
    configTime(utc_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");

    float temp = doc["current"]["temperature_2m"] | 0.0;
    int wcode = doc["current"]["weathercode"] | 0;
    int is_day = doc["current"]["is_day"] | 1;

    if(img_weather) {
        const lv_img_dsc_t* new_img = get_weather_image(wcode, is_day);
        lv_img_set_src(img_weather, new_img);
        Serial.println("[LVGL] Weather icon set");
        Serial.printf("  LVGL img src: %p\n", lv_img_get_src(img_weather));
        lv_obj_clear_flag(img_weather, LV_OBJ_FLAG_HIDDEN);
    }

    const char *desc = "Unknown";
    switch(wcode) {
        case 0: desc = "Clear sky"; break;
        case 1: desc = "Mainly clear"; break;
        case 2: desc = "Partly cloudy"; break;
        case 3: case 4: desc = "Overcast"; break;
        case 45: case 48: desc = "Fog"; break;
        case 51: case 53: case 55: desc = "Drizzle"; break;
        case 56: case 57: desc = "Freezing drizzle"; break;
        case 61: case 63: case 65: desc = "Rain"; break;
        case 66: case 67: desc = "Freezing rain"; break;
        case 71: case 73: case 75: case 77: desc = "Snow"; break;
        case 80: case 81: case 82: desc = "Rain showers"; break;
        case 85: case 86: case 87: case 88: case 89: case 90: desc = "Snow showers"; break;
        case 95: case 96: case 97: case 98: case 99: desc = "Thunderstorm"; break;
        default: desc = "Cloudy"; break;
    }
    char tempbuf[32];
    float temp_disp = temp;
    char unit = 'C';
    if (use_fahrenheit) {
        temp_disp = temp * 9.0 / 5.0 + 32.0;
        unit = 'F';
    }
    snprintf(tempbuf, sizeof(tempbuf), "%.1f°%c", temp_disp, unit);
    lv_label_set_text(lbl_temp, tempbuf);
    lv_label_set_text(lbl_desc, desc);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(theme_accent[dark_theme ? 1 : 0]), 0);

    bool rain_soon = false;
    bool rain_later = false;
    if(doc.containsKey("hourly")) {
        JsonArrayConst precipArr = doc["hourly"]["precipitation_probability"].as<JsonArrayConst>();
        if (precipArr.size() > 0) {
            for (uint16_t i = 0; i < precipArr.size() && i < 4; ++i) {
                float p = precipArr[i].as<float>();
                if (p > 40.0) {
                    if (i < 2) rain_soon = true;
                    else rain_later = true;
                    break;
                }
            }
        }
    }
    if (rain_soon) {
        lv_label_set_text(lbl_rain, "Rain expected\nsoon");
    } else if (rain_later) {
        lv_label_set_text(lbl_rain, "Rain expected\nlater today");
    } else {
        lv_label_set_text(lbl_rain, "No rain expected\nsoon");
    }
    lv_obj_set_width(lbl_rain, 160);
    lv_label_set_long_mode(lbl_rain, LV_LABEL_LONG_WRAP);

    if (doc.containsKey("hourly")) {
        JsonArrayConst hours = doc["hourly"]["time"].as<JsonArrayConst>();
        JsonArrayConst temps = doc["hourly"]["temperature_2m"].as<JsonArrayConst>();
        
        // Get current time to determine starting index
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            // Fallback to first 4 hours if time not available
            for (int i = 0; i < 4 && i < hours.size(); i++) {
                String tstr = hours[i].as<const char*>();
                int hour = atoi(tstr.substring(11,13).c_str());
                String hourstr;
                if (i == 0) hourstr = "Now";
                else if (use_24h) hourstr = (hour < 10 ? "0" : "") + String(hour) + ":00";
                else {
                    int h = hour % 12;
                    if (h == 0) h = 12;
                    hourstr = String(h) + (hour < 12 ? "am" : "pm");
                }
                float temp = temps[i].as<float>();
                if (use_fahrenheit) temp = temp * 9.0 / 5.0 + 32.0;
                lv_label_set_text(hourly_bar[i][0], hourstr.c_str());
                lv_label_set_text_fmt(hourly_bar[i][1], "%.0f°%c", temp, unit);
            }
        } else {
            // Calculate starting index based on current hour
            int current_hour = timeinfo.tm_hour;
            int start_index = current_hour;
            
            // Ensure we have at least 4 hours of data
            if (start_index + 4 >= hours.size()) {
                start_index = 0;
            }
            
            for (int i = 0; i < 4; i++) {
                int idx = start_index + i;
                if (idx < hours.size()) {
                    String tstr = hours[idx].as<const char*>();
                    int hour = atoi(tstr.substring(11,13).c_str());
                    String hourstr;
                    
                    if (i == 0) {
                        hourstr = "Now";
                    } else {
                        if (use_24h) {
                            hourstr = (hour < 10 ? "0" : "") + String(hour) + ":00";
                        } else {
                            int h = hour % 12;
                            if (h == 0) h = 12;
                            hourstr = String(h) + (hour < 12 ? "am" : "pm");
                        }
                    }
                    
                    float temp = temps[idx].as<float>();
                    if (use_fahrenheit) temp = temp * 9.0 / 5.0 + 32.0;
                    lv_label_set_text(hourly_bar[i][0], hourstr.c_str());
                    lv_label_set_text_fmt(hourly_bar[i][1], "%.0f°%c", temp, unit);
                } else {
                    // Not enough data
                    lv_label_set_text(hourly_bar[i][0], "--");
                    lv_label_set_text(hourly_bar[i][1], "--");
                }
            }
        }
    }

    if (doc.containsKey("daily")) {
        JsonArrayConst days = doc["daily"]["time"].as<JsonArrayConst>();
        JsonArrayConst tmax = doc["daily"]["temperature_2m_max"].as<JsonArrayConst>();
        JsonArrayConst tmin = doc["daily"]["temperature_2m_min"].as<JsonArrayConst>();
        JsonArrayConst precipProbArr = doc["daily"]["precipitation_probability_max"].as<JsonArrayConst>();
        int weekly_rows = 4;
        for (int i = 0; i < weekly_rows; i++) {
            String dstr = days[i].as<const char*>();
            String weekday = "Day";
            if (i == 0) weekday = "Today";
            else {
                struct tm tm{};
                strptime(dstr.c_str(), "%Y-%m-%d", &tm);
                static const char *wd[] = {"Sun","Mon","Tues","Wed","Thurs","Fri","Sat"};
                mktime(&tm);
                weekday = wd[tm.tm_wday];
            }
            float tmax_v = tmax[i].as<float>();
            float tmin_v = tmin[i].as<float>();
            if (use_fahrenheit) {
                tmax_v = tmax_v * 9.0 / 5.0 + 32.0;
                tmin_v = tmin_v * 9.0 / 5.0 + 32.0;
            }
            lv_label_set_text_fmt(weekly_row[i][0], "%s", weekday.c_str());
            lv_label_set_text_fmt(weekly_row[i][2], "%.0f°%c", tmax_v, unit);
            lv_label_set_text_fmt(weekly_row[i][3], "%.0f°%c", tmin_v, unit);
            if (i < precipProbArr.size()) {
                float precipProb = precipProbArr[i].as<float>();
                if (precipProb >= 20.0) {
                    if (precipProb < 50.0) {
                        lv_img_set_src(weekly_row[i][1], &drizzle);
                    } else if (precipProb < 80.0) {
                        lv_img_set_src(weekly_row[i][1], &showers_rain);
                    } else {
                        lv_img_set_src(weekly_row[i][1], &heavy_rain);
                    }
                    lv_obj_clear_flag(weekly_row[i][1], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(weekly_row[i][1], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    last_weather_update = millis();
}

void update_brightness() {
    if (night_mode) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            int hour = timeinfo.tm_hour;
            if (hour >= 22 || hour < 4) {
                analogWrite(LCD_BACKLIGHT_PIN, NIGHT_MODE_BRIGHTNESS);
                return;
            }
        }
    }
    analogWrite(LCD_BACKLIGHT_PIN, brightness);
}

void set_dark_theme(bool enabled) {
    dark_theme = enabled;
    apply_theme();
    update_menu_theme();
    pending_weather_update = true;
}

void show_splash() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(theme_bg_top[0]), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Daylite");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_42, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(theme_accent[0]), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -24);
    lv_obj_t *lbl2 = lv_label_create(scr);
    lv_label_set_text(lbl2, "for Home\nby Shivam Hegadi");
    lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl2, lv_color_hex(0xcad7e7), 0);
    lv_obj_align(lbl2, LV_ALIGN_CENTER, 0, 40);
}

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

void setup() {
    Serial.begin(115200);
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    analogWrite(LCD_BACKLIGHT_PIN, DEFAULT_BRIGHTNESS);

    prefs.begin("weather", false);
    latitude = prefs.getString("latitude", "51.5074");
    longitude = prefs.getString("longitude", "-0.1278");
    use_fahrenheit = prefs.getBool("useFahrenheit", false);
    use_24h = prefs.getBool("use24Hour", true);
    brightness = prefs.getUInt("brightness", DEFAULT_BRIGHTNESS);
    night_mode = prefs.getBool("night_mode", false);
    dark_theme = prefs.getBool("dark_theme", false);
    cur_city = prefs.getString("location", "London");

    lv_init();
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(0);

    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    static lv_color_t buf1[DRAW_BUF_SIZE];
    lv_display_t * disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // UPDATED INPUT DEVICE SETUP FOR LVGL v9
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchscreen_read);

    show_splash();
    delay(1400);

    setup_home_screen();
    set_dark_theme(dark_theme);

    WiFiManager wm;
    wm.setConfigPortalBlocking(true);
    wm.autoConnect("WeatherBoxAP");

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    lv_timer_create(update_time, 1000, NULL);
    lv_timer_create([](lv_timer_t *timer) {
        update_brightness();
    }, 60000, NULL);
    
    update_brightness();
    fetch_and_update_weather();
}

void loop() {
    lv_timer_handler();
    lv_tick_inc(5);
    delay(5);
    if (pending_weather_update) {
        fetch_and_update_weather();
        pending_weather_update = false;
    }
    if(millis() - last_weather_update > WEATHER_UPDATE_INTERVAL) {
        fetch_and_update_weather();
    }
}