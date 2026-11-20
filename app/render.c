#include <stdio.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "display.h"
#include "font8x8_basic.h"

#define W 480
#define H 270
#define HOR 92
#define BASE 250
#define LANE_W 118
#define DEPTH 9.0f
#define PI 3.14159265f

#define SKY_TOP 0x3E8EDE
#define SKY_LOW 0xBFE3FF
#define MOUNTAIN 0x7F9CC0
#define SNOW 0xF7FAFF
#define SNOW_FAR 0xDCE6F3
#define OFF_PISTE 0xE4ECF7
#define SHADOW 0xC3D0E2
#define MARKER 0xFF7A1A
#define PINE 0x1F6B3A
#define PINE_DARK 0x15502B
#define TRUNK 0x6B4226
#define ROCK 0x6E7781
#define ROCK_HI 0x99A2AD
#define LOG 0x8B5A2B
#define LOG_DARK 0x5E3A1A
#define GOLD 0xFFC93C
#define GOLD_HI 0xFFE9A6
#define JACKET 0xE53935
#define JACKET_HI 0xFF6F61
#define PANTS 0x263238
#define SKI 0xFFB300
#define HELMET 0x1565C0
#define POLE 0x37474F
#define INK 0x0D2440
#define WHITE 0xFFFFFF
#define YELLOW 0xFFD54F
#define MUTED 0xB8C7DC

static uint32_t px[W * H];
static unsigned frame;

static void rect(int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) px[j * W + i] = c;
}

static void shade(int x, int y, int w, int h, unsigned keep)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            uint32_t c = px[j * W + i];
            px[j * W + i] = ((c >> 16 & 255) * keep >> 8) << 16 | ((c >> 8 & 255) * keep >> 8) << 8 | ((c & 255) * keep >> 8);
        }
}

static void ell(int cx, int cy, int rx, int ry, uint32_t c)
{
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int j = -ry; j <= ry; j++) {
        int half = (int)(rx * sqrtf(1.0f - (float)(j * j) / (ry * ry)) + 0.5f);
        rect(cx - half, cy + j, 2 * half + 1, 1, c);
    }
}

static void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c)
{
    int minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    int miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    long area = (long)(x1 - x0) * (y2 - y0) - (long)(x2 - x0) * (y1 - y0);
    if (!area) return;
    for (int y = miny < 0 ? 0 : miny; y <= maxy && y < H; y++)
        for (int x = minx < 0 ? 0 : minx; x <= maxx && x < W; x++) {
            long a = (long)(x1 - x) * (y2 - y) - (long)(x2 - x) * (y1 - y);
            long b = (long)(x2 - x) * (y0 - y) - (long)(x0 - x) * (y2 - y);
            long d = (long)(x0 - x) * (y1 - y) - (long)(x1 - x) * (y0 - y);
            if ((area > 0 && a >= 0 && b >= 0 && d >= 0) || (area < 0 && a <= 0 && b <= 0 && d <= 0)) px[y * W + x] = c;
        }
}

static void text(int x, int y, int s, const char *str, uint32_t c)
{
    for (; *str; str++, x += 8 * s)
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if (font8x8_basic[*str & 0x7F][r] >> b & 1) rect(x + b * s, y + r * s, s, s, c);
}

static void text_c(int cx, int y, int s, const char *str, uint32_t c)
{
    int x = cx - (int)strlen(str) * 4 * s;
    text(x + s, y + s, s, str, INK);
    text(x, y, s, str, c);
}

static float persp(float z) { return DEPTH / (DEPTH + z); }
static int scr_y(float z) { return HOR + (int)((BASE - HOR) * persp(z)); }
static int scr_x(float lane, float z) { return W / 2 + (int)((lane - 1) * LANE_W * persp(z)); }

static void pine(int x, int y, int s)
{
    rect(x - s / 12, y - s / 4, s / 6 + 1, s / 4, TRUNK);
    tri(x - s / 2, y - s / 5, x + s / 2, y - s / 5, x, y - s * 3 / 5, PINE_DARK);
    tri(x - s * 2 / 5, y - s * 2 / 5, x + s * 2 / 5, y - s * 2 / 5, x, y - s * 4 / 5, PINE);
    tri(x - s * 3 / 10, y - s * 3 / 5, x + s * 3 / 10, y - s * 3 / 5, x, y - s, PINE);
    tri(x - s / 10, y - s * 9 / 10, x + s / 10, y - s * 9 / 10, x, y - s, SNOW);
}

static void scene(float dist)
{
    for (int y = 0; y < HOR; y++) {
        float t = (float)y / HOR;
        uint32_t c = (uint32_t)((SKY_TOP >> 16 & 255) * (1 - t) + (SKY_LOW >> 16 & 255) * t) << 16 |
                     (uint32_t)((SKY_TOP >> 8 & 255) * (1 - t) + (SKY_LOW >> 8 & 255) * t) << 8 |
                     (uint32_t)((SKY_TOP & 255) * (1 - t) + (SKY_LOW & 255) * t);
        rect(0, y, W, 1, c);
    }
    static const int peaks[][3] = { { 40, 40, 90 }, { 150, 22, 120 }, { 270, 34, 100 }, { 380, 18, 130 }, { 470, 44, 80 } };
    for (int i = 0; i < 5; i++) {
        int x = peaks[i][0], top = peaks[i][1], hw = peaks[i][2];
        tri(x - hw, HOR, x + hw, HOR, x, top, MOUNTAIN);
        tri(x - hw / 4, top + (HOR - top) / 4, x + hw / 4, top + (HOR - top) / 4, x, top, SNOW);
    }
    for (int y = HOR; y < H; y++) {
        float p = (float)(y - HOR) / (BASE - HOR);
        int edge = (int)(1.5f * LANE_W * p);
        rect(0, y, W, 1, y < HOR + 6 ? SNOW_FAR : OFF_PISTE);
        rect(W / 2 - edge, y, 2 * edge, 1, y < HOR + 6 ? SNOW_FAR : SNOW);
    }
    for (int k = 0; k < 26; k++) {
        float z = k * 3.0f - fmodf(dist, 3.0f);
        if (z < -1) continue;
        float p = persp(z);
        int s = 1 + (int)(3 * p);
        rect(scr_x(0.5f, z) - s / 2, scr_y(z) - s / 2, s, s, SHADOW);
        rect(scr_x(1.5f, z) - s / 2, scr_y(z) - s / 2, s, s, SHADOW);
    }
    for (int k = 12; k >= 0; k--) {
        float z = k * 6.0f - fmodf(dist, 6.0f);
        if (z < -2) continue;
        float p = persp(z);
        int h = (int)(26 * p) + 2, w = (int)(3 * p) + 1;
        rect(scr_x(-0.5f, z) - w / 2, scr_y(z) - h, w, h, MARKER);
        rect(scr_x(2.5f, z) - w / 2, scr_y(z) - h, w, h, MARKER);
    }
    for (int k = 9; k >= 0; k--) {
        float z = k * 8.0f - fmodf(dist + 4, 8.0f);
        if (z < -3) continue;
        int s = (int)(110 * persp(z)) + 4;
        pine(scr_x(-1.7f - (k % 2) * 0.5f, z), scr_y(z), s);
        pine(scr_x(3.7f + (k % 3) * 0.4f, z), scr_y(z), s);
    }
}

static void obstacle(const Obstacle *o, float t)
{
    float p = persp(o->z);
    int x = scr_x(o->lane, o->z), y = scr_y(o->z);
    switch (o->type) {
    case OBS_ROCK: {
        int s = (int)(48 * p) + 2;
        ell(x, y - s / 4, s / 2, s * 2 / 5, ROCK);
        ell(x - s / 8, y - s * 2 / 5, s / 4, s / 6, ROCK_HI);
        ell(x - s / 16, y - s * 3 / 5, s / 4, s / 12, SNOW);
        rect(x - s / 2, y, s + 1, s / 4 + 1, SNOW);
        rect(x - s / 2, y, s + 1, s / 12 + 1, SHADOW);
        break;
    }
    case OBS_TREE: {
        int s = (int)(100 * p) + 4;
        ell(x, y, s * 4 / 10, s / 14, SHADOW);
        pine(x, y, s);
        break;
    }
    case OBS_BRANCH: {
        int half = (int)(0.44f * LANE_W * p), h = (int)(56 * p) + 3, th = (int)(9 * p) + 2;
        rect(x - half, y - h, (int)(4 * p) + 1, h, LOG_DARK);
        rect(x + half - (int)(4 * p), y - h, (int)(4 * p) + 1, h, LOG_DARK);
        rect(x - half - (int)(6 * p), y - h + (int)(6 * p), 2 * half + (int)(12 * p), th, LOG);
        rect(x - half - (int)(6 * p), y - h + (int)(6 * p), 2 * half + (int)(12 * p), th / 3 + 1, SNOW);
        rect(x - half - (int)(6 * p), y - h + (int)(6 * p) + th - 1, 2 * half + (int)(12 * p), 1, LOG_DARK);
        break;
    }
    case OBS_COIN: {
        int r = (int)(10 * p) + 1, cy = y - (int)(22 * p) - (int)(3 * p * sinf(t * 4 + o->z));
        float spin = fabsf(cosf(t * 5 + o->z * 0.7f));
        ell(x, y, r * 2 / 3, r / 4, SHADOW);
        ell(x, cy, (int)(r * spin) + 1, r, 0xC48A00);
        ell(x, cy, (int)(r * spin * 0.75f), r * 3 / 4, GOLD);
        ell(x - (int)(r * spin * 0.25f), cy - r / 3, (int)(r * spin * 0.2f), r / 4, GOLD_HI);
        break;
    }
    }
}

static void skier(const Game *g)
{
    int x = W / 2 + (int)((g->lane_x - 1) * LANE_W), dx = (int)((g->lane - g->lane_x) * 10), lift = 0;
    bool crouch = g->pose == POSE_CROUCH;
    if (g->pose == POSE_JUMP) lift = (int)(sinf(PI * (1 - g->pose_t / JUMP_TIME)) * 42);
    int yb = BASE - lift, leg = crouch ? 11 : 20, torso = crouch ? 17 : 22, top = yb - leg - torso;
    ell(x, BASE + 2, 22 - lift / 5, 4, SHADOW);
    rect(x - 11, yb - 16, 4, 18, SKI);
    rect(x + 7, yb - 16, 4, 18, SKI);
    rect(x - 10, yb - leg, 8, leg, PANTS);
    rect(x + 2, yb - leg, 8, leg, PANTS);
    rect(x - 20 + dx / 2, top + 8, 2, leg + torso - 6, POLE);
    rect(x + 18 + dx / 2, top + 8, 2, leg + torso - 6, POLE);
    rect(x - 12 + dx / 2, top, 24, torso, JACKET);
    rect(x - 18 + dx / 2, top + 4, 7, 5, JACKET);
    rect(x + 11 + dx / 2, top + 4, 7, 5, JACKET);
    rect(x - 6 + dx / 2, top + 4, 12, torso - 8, JACKET_HI);
    ell(x + dx, top - 7, 8, 8, HELMET);
    rect(x + dx - 8, top - 6, 17, 2, PANTS);
}

static void hud(const Game *g)
{
    char b[32];
    snprintf(b, sizeof b, "SCORE %d", g->score);
    shade(4, 4, (int)strlen(b) * 16 + 12, 36, 150);
    text(10, 9, 2, b, WHITE);
    snprintf(b, sizeof b, "SPEED %d", (int)g->speed);
    text(10, 28, 1, b, MUTED);
    snprintf(b, sizeof b, "x %d", g->coins);
    int w = (int)strlen(b) * 16 + 34;
    shade(W - w - 4, 4, w, 26, 150);
    ell(W - w + 9, 16, 7, 7, GOLD);
    ell(W - w + 9, 16, 4, 4, GOLD_HI);
    text(W - w + 22, 9, 2, b, WHITE);
}

static void banner(const char *s)
{
    int scale = strlen(s) > 7 ? 4 : 5, w = (int)strlen(s) * 8 * scale + 24, h = 8 * scale + 20;
    shade(W / 2 - w / 2, 100 - h / 2, w, h, 110);
    text_c(W / 2, 100 - 4 * scale, scale, s, YELLOW);
}

bool render_init(void) { return display_open(W, H); }

void render_game(const Game *g, const char *overlay)
{
    int idx[MAX_OBS], n = 0;
    frame++;
    scene(g->distance);
    for (int i = 0; i < MAX_OBS; i++)
        if (g->obs[i].active && g->obs[i].z > -1.5f && g->obs[i].z < Z_FAR + 6) idx[n++] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && g->obs[idx[j]].z > g->obs[idx[j - 1]].z; j--) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
    int k = 0;
    for (; k < n && g->obs[idx[k]].z > 0.4f; k++) obstacle(&g->obs[idx[k]], g->time);
    skier(g);
    for (; k < n; k++) obstacle(&g->obs[idx[k]], g->time);
    hud(g);
    if (overlay) banner(overlay);
    display_present(px);
}

static void backdrop(void)
{
    frame++;
    scene(frame * 0.35f);
    shade(0, 0, W, H, 100);
}

void render_menu(const char *title, const char *const items[], int n, int sel, const char *footer)
{
    backdrop();
    text_c(W / 2, 22, 4, title, WHITE);
    rect(W / 2 - 90, 60, 180, 2, YELLOW);
    for (int i = 0; i < n; i++) {
        int y = 82 + i * 30 * 5 / (n > 5 ? n : 5);
        if (i == sel) {
            int w = (int)strlen(items[i]) * 16 + 40;
            shade(W / 2 - w / 2, y - 5, w, 26, 150);
            text_c(W / 2, y, 2, items[i], YELLOW);
            text(W / 2 - w / 2 + 6, y, 2, ">", YELLOW);
        } else text_c(W / 2, y, 2, items[i], MUTED);
    }
    if (footer) text_c(W / 2, H - 16, 1, footer, MUTED);
    display_present(px);
}

void render_lines(const char *title, const char *const lines[], int n)
{
    int gap = n > 7 ? 18 : 24;
    backdrop();
    text_c(W / 2, 18, 3, title, YELLOW);
    for (int i = 0; i < n; i++) text_c(W / 2, 62 + i * gap, 2, lines[i], WHITE);
    display_present(px);
}

bool render_screenshot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint8_t c[3] = { px[i] >> 16, px[i] >> 8, px[i] };
        fwrite(c, 1, 3, f);
    }
    return fclose(f) == 0;
}

void render_close(void) { display_close(); }
