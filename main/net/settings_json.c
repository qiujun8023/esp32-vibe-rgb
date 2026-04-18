#include "settings_json.h"

#include <lwip/inet.h>
#include <string.h>

char* settings_to_json(const settings_t* s) {
    cJSON* root = cJSON_CreateObject();

    /* 不回传密码，避免前端看到明文 */
    cJSON_AddStringToObject(root, "ssid", s->ssid);
    cJSON_AddNumberToObject(root, "ip_mode", s->ip_mode);

    struct in_addr a;
    char           ip_s[20];
    a.s_addr = s->s_ip;
    cJSON_AddStringToObject(root, "s_ip", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_mask;
    cJSON_AddStringToObject(root, "s_mask", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_gw;
    cJSON_AddStringToObject(root, "s_gw", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns1;
    cJSON_AddStringToObject(root, "s_dns1", inet_ntoa_r(a, ip_s, sizeof(ip_s)));
    a.s_addr = s->s_dns2;
    cJSON_AddStringToObject(root, "s_dns2", inet_ntoa_r(a, ip_s, sizeof(ip_s)));

    cJSON_AddNumberToObject(root, "led_gpio", s->led_gpio);
    cJSON_AddNumberToObject(root, "led_w", s->led_w);
    cJSON_AddNumberToObject(root, "led_h", s->led_h);
    cJSON_AddNumberToObject(root, "led_serpentine", s->led_serpentine);
    cJSON_AddNumberToObject(root, "led_start", s->led_start);
    cJSON_AddNumberToObject(root, "led_rotation", s->led_rotation);
    cJSON_AddNumberToObject(root, "brightness", s->brightness);

    cJSON_AddNumberToObject(root, "mic_sck", s->mic_sck);
    cJSON_AddNumberToObject(root, "mic_ws", s->mic_ws);
    cJSON_AddNumberToObject(root, "mic_din", s->mic_din);
    cJSON_AddNumberToObject(root, "agc_mode", s->agc_mode);
    cJSON_AddNumberToObject(root, "gain", s->gain);
    cJSON_AddNumberToObject(root, "squelch", s->squelch);
    cJSON_AddNumberToObject(root, "fft_smooth", s->fft_smooth);

    cJSON_AddNumberToObject(root, "effect", s->effect);
    cJSON_AddNumberToObject(root, "palette", s->palette);
    cJSON_AddNumberToObject(root, "speed", s->speed);
    cJSON_AddNumberToObject(root, "intensity", s->intensity);
    cJSON_AddNumberToObject(root, "custom1", s->custom1);
    cJSON_AddNumberToObject(root, "custom2", s->custom2);
    cJSON_AddNumberToObject(root, "custom3", s->custom3);

    cJSON* params_arr = cJSON_CreateArray();
    for (int i = 0; i < EFFECT_COUNT; i++) {
        cJSON* effect_arr = cJSON_CreateArray();
        cJSON_AddItemToArray(effect_arr, cJSON_CreateNumber(s->effect_params[i][0]));
        cJSON_AddItemToArray(effect_arr, cJSON_CreateNumber(s->effect_params[i][1]));
        cJSON_AddItemToArray(effect_arr, cJSON_CreateNumber(s->effect_params[i][2]));
        cJSON_AddItemToArray(params_arr, effect_arr);
    }
    cJSON_AddItemToObject(root, "effect_params", params_arr);

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

bool settings_from_cjson(cJSON* root, settings_t* s, bool* need_restart) {
    if (!root || !s || !need_restart) return false;
    *need_restart = false;

#define GET_INT(key, field)                                                      \
    {                                                                            \
        cJSON* it = cJSON_GetObjectItem(root, key);                              \
        if (it && cJSON_IsNumber(it)) s->field = (typeof(s->field))it->valueint; \
    }

#define GET_FLT(key, field)                                              \
    {                                                                    \
        cJSON* it = cJSON_GetObjectItem(root, key);                      \
        if (it && cJSON_IsNumber(it)) s->field = (float)it->valuedouble; \
    }

#define WATCH_INT(key, field)                                                     \
    {                                                                             \
        cJSON* it = cJSON_GetObjectItem(root, key);                               \
        if (it && cJSON_IsNumber(it)) {                                           \
            if (s->field != (typeof(s->field))it->valueint) *need_restart = true; \
            s->field = (typeof(s->field))it->valueint;                            \
        }                                                                         \
    }

    /* 空字符串不覆盖，前端未填写时保留旧值 */
    cJSON* ssid_it = cJSON_GetObjectItem(root, "ssid");
    if (ssid_it && cJSON_IsString(ssid_it) && ssid_it->valuestring[0]) {
        if (strcmp(s->ssid, ssid_it->valuestring)) *need_restart = true;
        strlcpy(s->ssid, ssid_it->valuestring, sizeof(s->ssid));
    }

    /* 密码用 pass_new 字段，避免与回传的占位字段混淆 */
    cJSON* pass_it = cJSON_GetObjectItem(root, "pass_new");
    if (pass_it && cJSON_IsString(pass_it) && pass_it->valuestring[0]) {
        if (strcmp(s->pass, pass_it->valuestring)) *need_restart = true;
        strlcpy(s->pass, pass_it->valuestring, sizeof(s->pass));
    }

    WATCH_INT("led_gpio", led_gpio);
    WATCH_INT("led_w", led_w);
    WATCH_INT("led_h", led_h);
    WATCH_INT("mic_sck", mic_sck);
    WATCH_INT("mic_ws", mic_ws);
    WATCH_INT("mic_din", mic_din);

    GET_INT("led_serpentine", led_serpentine);
    GET_INT("led_start", led_start);
    GET_INT("led_rotation", led_rotation);
    GET_INT("ip_mode", ip_mode);
    GET_INT("brightness", brightness);
    GET_INT("agc_mode", agc_mode);
    GET_FLT("gain", gain);
    GET_INT("squelch", squelch);
    GET_INT("fft_smooth", fft_smooth);
    uint8_t old_effect = s->effect;
    GET_INT("effect", effect);

    /* 切换效果前把当前 custom1/2/3 存回旧 slot，再从新 slot 加载 */
    if (s->effect != old_effect && s->effect < EFFECT_COUNT) {
        s->effect_params[old_effect][0] = s->custom1;
        s->effect_params[old_effect][1] = s->custom2;
        s->effect_params[old_effect][2] = s->custom3;
        s->custom1                      = s->effect_params[s->effect][0];
        s->custom2                      = s->effect_params[s->effect][1];
        s->custom3                      = s->effect_params[s->effect][2];
    }

    GET_INT("palette", palette);
    GET_INT("speed", speed);
    GET_INT("intensity", intensity);

    /* 完整恢复场景（例如前端一次性 POST 全量配置）才会带 effect_params */
    cJSON* params_arr = cJSON_GetObjectItem(root, "effect_params");
    if (params_arr && cJSON_IsArray(params_arr)) {
        int count = cJSON_GetArraySize(params_arr);
        for (int i = 0; i < count && i < EFFECT_COUNT; i++) {
            cJSON* effect_arr = cJSON_GetArrayItem(params_arr, i);
            if (effect_arr && cJSON_IsArray(effect_arr)) {
                for (int j = 0; j < 3; j++) {
                    cJSON* val = cJSON_GetArrayItem(effect_arr, j);
                    if (val && cJSON_IsNumber(val)) {
                        s->effect_params[i][j] = (uint8_t)val->valueint;
                    }
                }
            }
        }
        s->custom1 = s->effect_params[s->effect][0];
        s->custom2 = s->effect_params[s->effect][1];
        s->custom3 = s->effect_params[s->effect][2];
    }

    /* 单字段更新优先级最高，覆盖上面数组同步的结果 */
    cJSON* c1_it = cJSON_GetObjectItem(root, "custom1");
    cJSON* c2_it = cJSON_GetObjectItem(root, "custom2");
    cJSON* c3_it = cJSON_GetObjectItem(root, "custom3");
    if (c1_it && cJSON_IsNumber(c1_it)) {
        s->custom1                     = (uint8_t)c1_it->valueint;
        s->effect_params[s->effect][0] = s->custom1;
    }
    if (c2_it && cJSON_IsNumber(c2_it)) {
        s->custom2                     = (uint8_t)c2_it->valueint;
        s->effect_params[s->effect][1] = s->custom2;
    }
    if (c3_it && cJSON_IsNumber(c3_it)) {
        s->custom3                     = (uint8_t)c3_it->valueint;
        s->effect_params[s->effect][2] = s->custom3;
    }

    cJSON* it;
    if ((it = cJSON_GetObjectItem(root, "s_ip")) && cJSON_IsString(it)) s->s_ip = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_mask")) && cJSON_IsString(it)) s->s_mask = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_gw")) && cJSON_IsString(it)) s->s_gw = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns1")) && cJSON_IsString(it)) s->s_dns1 = inet_addr(it->valuestring);
    if ((it = cJSON_GetObjectItem(root, "s_dns2")) && cJSON_IsString(it)) s->s_dns2 = inet_addr(it->valuestring);

#undef GET_INT
#undef GET_FLT
#undef WATCH_INT

    return true;
}