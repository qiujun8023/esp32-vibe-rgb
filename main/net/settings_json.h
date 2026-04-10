/**
 * @file settings_json.h
 * @brief settings 与 JSON 的序列化/反序列化接口
 *
 * 所有函数在锁外操作，调用方负责传入快照或已持锁的指针
 */

#pragma once

#include <cJSON.h>
#include <stdbool.h>

#include "settings.h"

/**
 * @brief 将 settings 快照序列化为 JSON 字符串
 *
 * @return JSON 字符串，调用方需要 free()
 */
char* settings_to_json(const settings_t* snap);

/**
 * @brief 从 cJSON 对象读取字段写入 settings
 *
 * @param root 已解析的 cJSON 对象
 * @param s settings 指针（调用方负责加锁）
 * @param need_restart 如有需要重启的字段改动则置 true
 */
bool settings_from_cjson(cJSON* root, settings_t* s, bool* need_restart);