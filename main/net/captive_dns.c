#include "captive_dns.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char* TAG = "captive_dns";

/* RFC1035 4.1.1 报文头偏移:flags 字段位于 2-3,ancount 位于 6-7 */
#define DNS_HDR_LEN      12
#define DNS_FLAGS_HI     2
#define DNS_FLAGS_LO     3
#define DNS_ANCOUNT_HI   6
#define DNS_ANCOUNT_LO   7

/* QR=1 response, AA=1 authoritative, RD=1, RA=1 */
#define DNS_FLAGS_HI_RESP 0x81
#define DNS_FLAGS_LO_RESP 0x80
#define DNS_ANSWER_COUNT  0x01

/* 固定 16 字节应答:2 ptr + 2 type + 2 class + 4 ttl + 2 rdlen + 4 ip */
#define DNS_ANSWER_LEN 16

/* 所有 A 查询都劫持到 AP IP */
static const uint8_t AP_IP[4] = {192, 168, 4, 1};
static const uint32_t A_TTL_SEC = 60;

static void dns_task(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket create failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind port 53 failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    /* 查询缓冲原地改写并追加 answer 回发,省掉一份 512 字节栈副本 */
    uint8_t            buf[512];
    struct sockaddr_in cli;

    while (1) {
        socklen_t clen = sizeof(cli);
        int       n    = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&cli, &clen);
        /* 丢弃过短查询和追加应答后会溢出 buf 的长帧 */
        if (n < DNS_HDR_LEN || n > (int)sizeof(buf) - DNS_ANSWER_LEN)
            continue;

        uint8_t* resp = buf;
        resp[DNS_FLAGS_HI]   = DNS_FLAGS_HI_RESP;
        resp[DNS_FLAGS_LO]   = DNS_FLAGS_LO_RESP;
        resp[DNS_ANCOUNT_HI] = 0x00;
        resp[DNS_ANCOUNT_LO] = DNS_ANSWER_COUNT;

        int p = n;
        /* NAME: 指针压缩 0xC00C,指向 header 末尾后的问题名字段 */
        resp[p++] = 0xC0;
        resp[p++] = 0x0C;
        /* TYPE=A */
        resp[p++] = 0x00;
        resp[p++] = 0x01;
        /* CLASS=IN */
        resp[p++] = 0x00;
        resp[p++] = 0x01;
        resp[p++] = (A_TTL_SEC >> 24) & 0xFF;
        resp[p++] = (A_TTL_SEC >> 16) & 0xFF;
        resp[p++] = (A_TTL_SEC >> 8) & 0xFF;
        resp[p++] = A_TTL_SEC & 0xFF;
        /* RDLENGTH=4 */
        resp[p++] = 0x00;
        resp[p++] = 0x04;
        resp[p++] = AP_IP[0];
        resp[p++] = AP_IP[1];
        resp[p++] = AP_IP[2];
        resp[p++] = AP_IP[3];

        sendto(sock, resp, p, 0, (struct sockaddr*)&cli, clen);
    }
}

void captive_dns_start(void) {
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "captive dns ready");
}
