#pragma once

// net/net_init.h
// 网络栈统一初始化入口，在 app_main 中调用一次
// wifi_prov 和 wifi_sta 均不再自行初始化 esp_netif / event_loop

void net_init(void);
