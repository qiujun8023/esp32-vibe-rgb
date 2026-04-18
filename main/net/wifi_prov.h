#pragma once

/* 启动配网 AP,阻塞直到用户提交表单,内部会 esp_restart */
void wifi_prov_start_ap(void);
