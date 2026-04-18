#include "led_priv.h"

int     s_w = 8, s_h = 8;
uint8_t s_serpentine = 0, s_start = 0, s_rotation = 0;
int16_t s_lookup[LED_MAX_COUNT];

static int get_physical_idx(int px, int py) {
    if (px < 0 || px >= s_w || py < 0 || py >= s_h) return -1;

    int x = (s_start & 1) ? (s_w - 1 - px) : px;
    int y = (s_start & 2) ? (s_h - 1 - py) : py;

    /* 蛇形走线在 start 翻转之后再做奇偶行反向，否则起点不在角落时会错位 */
    if (s_serpentine && (y & 1)) {
        x = (s_w - 1) - x;
    }

    return y * s_w + x;
}

void led_map_rebuild(void) {
    int lw = led_width();
    int lh = led_height();

    for (int ly = 0; ly < lh; ly++) {
        for (int lx = 0; lx < lw; lx++) {
            int px, py;
            switch (s_rotation) {
                case 1:
                    px = ly;
                    py = s_h - 1 - lx;
                    break;
                case 2:
                    px = s_w - 1 - lx;
                    py = s_h - 1 - ly;
                    break;
                case 3:
                    px = s_w - 1 - ly;
                    py = lx;
                    break;
                default:
                    px = lx;
                    py = ly;
                    break;
            }
            s_lookup[ly * lw + lx] = get_physical_idx(px, py);
        }
    }
}

int led_width(void) {
    return (s_rotation & 1) ? s_h : s_w;
}

int led_height(void) {
    return (s_rotation & 1) ? s_w : s_h;
}

int led_count(void) {
    return s_w * s_h;
}