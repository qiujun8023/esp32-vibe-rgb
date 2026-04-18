#pragma once

/* 启动 DNS 劫持:所有 A 查询统一返回 192.168.4.1 */
void captive_dns_start(void);
