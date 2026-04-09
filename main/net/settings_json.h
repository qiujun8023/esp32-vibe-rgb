#pragma once

// net/settings_json.h
// settings 与 JSON 的序列化/反序列化
// 所有函数均在锁外操作，调用方负责传入快照或已持锁的指针

#include <cJSON.h>
#include <stdbool.h>

#include "settings.h"

// 将 settings 快照序列化为 JSON 字符串（调用方 free()）
char* settings_to_json(const settings_t* snap);

// 从已解析的 cJSON 对象中读取字段并写入 *s
// need_restart：如有需要重启的字段改动（如 GPIO/WiFi），置 true
// 调用方自行处理 settings_lock/unlock
bool settings_from_cjson(cJSON* root, settings_t* s, bool* need_restart);
