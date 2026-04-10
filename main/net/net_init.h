/**
 * @file net_init.h
 * @brief 网络栈初始化入口
 *
 * 在 app_main 中调用一次，wifi_prov 和 wifi_sta 不再自行初始化
 */

#pragma once

void net_init(void);