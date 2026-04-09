// net/settings_json.c
// settings ↔ JSON 序列化/反序列化
// 修复：
//   1. settings_to_json 接受快照指针，不持锁，避免持锁期间堆分配
//   2. settings_from_cjson 接受已解析的 cJSON*，避免二次解析

#include "settings_json.h"

#include <lwip/inet.h>
#include <string.h>

// ── 序列化：settings → JSON 字符串 ───────────────────────────────────────────
char* settings_to_json(const settings_t* s) {
    cJSON* root = cJSON_CreateObject();

    // WiFi
    cJSON_AddStringToObject(root, "ssid",    s->ssid);
    cJSON_AddNumberToObject(root, "ip_mode", s->ip_mode);

    struct in_addr a;
    char           ip_s[20];
    a.s_addr = s->s_ip;   cJSON_AddStringToObject(root, "s_ip",   inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_mask; cJSON_AddStringToObject(root, "s_mask", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_gw;   cJSON_AddStringToObject(root, "s_gw",   inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns1; cJSON_AddStringToObject(root, "s_dns1", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns2; cJSON_AddStringToObject(root, "s_dns2", inet_ntoa_r(a, ip_s, sizeof(ip_s)));

    // LED
    cJSON_AddNumberToObject(root, "led_gpio",       s->led_gpio);
    cJSON_AddNumberToObject(root, "led_w",          s->led_w);
    cJSON_AddNumberToObject(root, "led_h",          s->led_h);
    cJSON_AddNumberToObject(root, "led_serpentine", s->led_serpentine);
    cJSON_AddNumberToObject(root, "led_start",      s->led_start);
    cJSON_AddNumberToObject(root, "led_rotation",   s->led_rotation);
    cJSON_AddNumberToObject(root, "brightness",     s->brightness);

    // 麦克风
    cJSON_AddNumberToObject(root, "mic_sck",    s->mic_sck);
    cJSON_AddNumberToObject(root, "mic_ws",     s->mic_ws);
    cJSON_AddNumberToObject(root, "mic_din",    s->mic_din);
    cJSON_AddNumberToObject(root, "agc_mode",   s->agc_mode);
    cJSON_AddNumberToObject(root, "gain",       s->gain);
    cJSON_AddNumberToObject(root, "squelch",    s->squelch);
    cJSON_AddNumberToObject(root, "fft_smooth", s->fft_smooth);

    // 特效
    cJSON_AddNumberToObject(root, "effect",    s->effect);
    cJSON_AddNumberToObject(root, "palette",   s->palette);
    cJSON_AddNumberToObject(root, "speed",     s->speed);
    cJSON_AddNumberToObject(root, "intensity", s->intensity);
    cJSON_AddNumberToObject(root, "custom1",   s->custom1);
    cJSON_AddNumberToObject(root, "custom2",   s->custom2);
    cJSON_AddNumberToObject(root, "custom3",   s->custom3);
    cJSON_AddNumberToObject(root, "freq_dir",  s->freq_dir);

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

// ── 反序列化：cJSON* → settings 字段（避免二次解析） ─────────────────────────
bool settings_from_cjson(cJSON* root, settings_t* s, bool* need_restart) {
    if (!root || !s || !need_restart) return false;
    *need_restart = false;

#define GET_INT(key, field) \
    { cJSON* it = cJSON_GetObjectItem(root, key); \
      if (it && cJSON_IsNumber(it)) s->field = (typeof(s->field))it->valueint; }

#define GET_FLT(key, field) \
    { cJSON* it = cJSON_GetObjectItem(root, key); \
      if (it && cJSON_IsNumber(it)) s->field = (float)it->valuedouble; }

#define WATCH_INT(key, field) \
    { cJSON* it = cJSON_GetObjectItem(root, key); \
      if (it && cJSON_IsNumber(it)) { \
          if (s->field != (typeof(s->field))it->valueint) *need_restart = true; \
          s->field = (typeof(s->field))it->valueint; \
      } }

    // WiFi SSID/密码（非空才更新）
    cJSON* ssid_it = cJSON_GetObjectItem(root, "ssid");
    if (ssid_it && cJSON_IsString(ssid_it) && ssid_it->valuestring[0]) {
        if (strcmp(s->ssid, ssid_it->valuestring)) *need_restart = true;
        strlcpy(s->ssid, ssid_it->valuestring, sizeof(s->ssid));
    }
    cJSON* pass_it = cJSON_GetObjectItem(root, "pass_new");
    if (pass_it && cJSON_IsString(pass_it) && pass_it->valuestring[0]) {
        if (strcmp(s->pass, pass_it->valuestring)) *need_restart = true;
        strlcpy(s->pass, pass_it->valuestring, sizeof(s->pass));
    }

    // 需要重启的字段
    WATCH_INT("led_gpio",       led_gpio);
    WATCH_INT("led_w",          led_w);
    WATCH_INT("led_h",          led_h);
    WATCH_INT("led_serpentine", led_serpentine);
    WATCH_INT("led_start",      led_start);
    WATCH_INT("mic_sck",        mic_sck);
    WATCH_INT("mic_ws",         mic_ws);
    WATCH_INT("mic_din",        mic_din);

    // 普通字段
    GET_INT("led_rotation", led_rotation);
    GET_INT("ip_mode",      ip_mode);
    GET_INT("brightness",   brightness);
    GET_INT("agc_mode",     agc_mode);
    GET_FLT("gain",         gain);
    GET_INT("squelch",      squelch);
    GET_INT("fft_smooth",   fft_smooth);
    GET_INT("effect",       effect);
    GET_INT("palette",      palette);
    GET_INT("speed",        speed);
    GET_INT("intensity",    intensity);
    GET_INT("custom1",      custom1);
    GET_INT("custom2",      custom2);
    GET_INT("custom3",      custom3);
    GET_INT("freq_dir",     freq_dir);

    // 静态 IP 字段
    cJSON* it;
    if ((it = cJSON_GetObjectItem(root, "s_ip"))   && cJSON_IsString(it)) s->s_ip   = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_mask"))  && cJSON_IsString(it)) s->s_mask = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_gw"))   && cJSON_IsString(it)) s->s_gw   = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns1")) && cJSON_IsString(it)) s->s_dns1 = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns2")) && cJSON_IsString(it)) s->s_dns2 = inet_addr(it->valuestring);

#undef GET_INT
#undef GET_FLT
#undef WATCH_INT

    return true;
}
