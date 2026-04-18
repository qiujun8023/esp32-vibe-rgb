#pragma once

#include <cJSON.h>
#include <stdbool.h>

#include "settings.h"

/* 返回堆上分配的 JSON 字符串,调用方 free */
char* settings_to_json(const settings_t* snap);

/* root: 已解析的 cJSON;s: 调用方需先持锁;need_restart: 出参,关键字段变化时置 true */
bool settings_from_cjson(cJSON* root, settings_t* s, bool* need_restart);
