#pragma once
// STA 连接 + HTTP+WebSocket 控制服务器
// 失败则清 WiFi 凭据后重启（回到配网）
void wifi_ctrl_init(void);
