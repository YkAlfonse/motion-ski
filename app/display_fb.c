#ifndef USE_SDL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include "display.h"

typedef enum { OUT_NONE, OUT_HEADLESS, OUT_FB, OUT_DRM } Out;

static Out out;
static int fd = -1, tty = -1, cw, ch, xres, yres, pitch, bytes, scale, ox, oy;
static uint8_t *mem, *base;
static size_t size;
static uint32_t ro = 16, go = 8, bo = 0;
static uint8_t line[4096 * 4];
static uint32_t drm_handle, drm_fb, drm_conn;
static struct drm_mode_crtc saved_crtc;

static void layout(void)
{
    int sx = xres / cw, sy = yres / ch;
    scale = sx < sy ? sx : sy;
    if (scale < 1) scale = 1;
    ox = (xres - cw * scale) / 2;
    oy = (yres - ch * scale) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    memset(base, 0, (size_t)pitch * yres);
}

static bool open_fb(void)
{
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if ((fd = open("/dev/fb0", O_RDWR)) < 0) return false;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) || ioctl(fd, FBIOGET_FSCREENINFO, &fi) ||
        (vi.bits_per_pixel != 32 && vi.bits_per_pixel != 16)) { close(fd); fd = -1; return false; }
    size = fi.smem_len;
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { close(fd); fd = -1; mem = NULL; return false; }
    xres = vi.xres;
    yres = vi.yres;
    pitch = fi.line_length;
    bytes = vi.bits_per_pixel / 8;
    ro = vi.red.offset;
    go = vi.green.offset;
    bo = vi.blue.offset;
    base = mem + (size_t)vi.yoffset * pitch + (size_t)vi.xoffset * bytes;
    if ((tty = open("/dev/tty0", O_RDWR)) >= 0 || (tty = open("/dev/tty1", O_RDWR)) >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS);
    return true;
}

static void *get(int req, void *arg) { return ioctl(fd, req, arg) ? NULL : arg; }

static bool pick_crtc(struct drm_mode_card_res *res, uint32_t *crtcs, struct drm_mode_get_connector *c, uint32_t *encs, uint32_t *crtc)
{
    struct drm_mode_get_encoder e = { .encoder_id = c->encoder_id };
    if (c->encoder_id && get(DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id) { *crtc = e.crtc_id; return true; }
    for (uint32_t i = 0; i < c->count_encoders; i++) {
        e = (struct drm_mode_get_encoder){ .encoder_id = encs[i] };
        if (!get(DRM_IOCTL_MODE_GETENCODER, &e)) continue;
        for (uint32_t j = 0; j < res->count_crtcs; j++)
            if (e.possible_crtcs & (1u << j)) { *crtc = crtcs[j]; return true; }
    }
    return false;
}

static bool open_drm_card(const char *path)
{
    struct drm_mode_card_res res = { 0 };
    uint32_t conns[16], crtcs[16], encs[16], crtc = 0;
    struct drm_mode_modeinfo modes[64], mode;
    bool found = false;
    if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) return false;
    if (!get(DRM_IOCTL_MODE_GETRESOURCES, &res) || res.count_connectors > 16 || res.count_crtcs > 16) goto fail;
    res = (struct drm_mode_card_res){ .connector_id_ptr = (uintptr_t)conns, .crtc_id_ptr = (uintptr_t)crtcs,
                                      .count_connectors = res.count_connectors, .count_crtcs = res.count_crtcs };
    if (!get(DRM_IOCTL_MODE_GETRESOURCES, &res)) goto fail;
    for (uint32_t i = 0; i < res.count_connectors && !found; i++) {
        struct drm_mode_get_connector c = { .connector_id = conns[i] };
        if (!get(DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.connection != 1 || !c.count_modes) continue;
        c = (struct drm_mode_get_connector){ .connector_id = conns[i], .modes_ptr = (uintptr_t)modes, .encoders_ptr = (uintptr_t)encs,
                                             .count_modes = c.count_modes > 64 ? 64 : c.count_modes,
                                             .count_encoders = c.count_encoders > 16 ? 16 : c.count_encoders };
        if (!get(DRM_IOCTL_MODE_GETCONNECTOR, &c) || !pick_crtc(&res, crtcs, &c, encs, &crtc)) continue;
        mode = modes[0];
        for (uint32_t m = 0; m < c.count_modes; m++) if (modes[m].type & DRM_MODE_TYPE_PREFERRED) { mode = modes[m]; break; }
        for (uint32_t m = 0; m < c.count_modes; m++) if (modes[m].hdisplay == 1920 && modes[m].vdisplay == 1080) { mode = modes[m]; break; }
        drm_conn = conns[i];
        found = true;
    }
    if (!found) goto fail;
    struct drm_mode_create_dumb cd = { .width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32 };
    if (!get(DRM_IOCTL_MODE_CREATE_DUMB, &cd)) goto fail;
    drm_handle = cd.handle;
    struct drm_mode_fb_cmd fb = { .width = cd.width, .height = cd.height, .pitch = cd.pitch, .bpp = 32, .depth = 24, .handle = cd.handle };
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (!get(DRM_IOCTL_MODE_ADDFB, &fb) || !get(DRM_IOCTL_MODE_MAP_DUMB, &md)) goto fail;
    drm_fb = fb.fb_id;
    size = cd.size;
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (mem == MAP_FAILED) { mem = NULL; goto fail; }
    saved_crtc = (struct drm_mode_crtc){ .crtc_id = crtc };
    get(DRM_IOCTL_MODE_GETCRTC, &saved_crtc);
    struct drm_mode_crtc set = { .set_connectors_ptr = (uintptr_t)&drm_conn, .count_connectors = 1, .crtc_id = crtc,
                                 .fb_id = drm_fb, .mode_valid = 1, .mode = mode };
    if (!get(DRM_IOCTL_MODE_SETCRTC, &set)) goto fail;
    xres = mode.hdisplay;
    yres = mode.vdisplay;
    pitch = cd.pitch;
    bytes = 4;
    base = mem;
    return true;
fail:
    if (mem) munmap(mem, size);
    mem = NULL;
    close(fd);
    fd = -1;
    return false;
}

static bool open_drm(void)
{
    char path[32];
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "/dev/dri/card%d", i);
        if (open_drm_card(path)) return true;
    }
    return false;
}

bool display_open(int w, int h)
{
    const char *want = getenv("MOTION_SKI_DISPLAY");
    cw = w;
    ch = h;
    if (want && !strcmp(want, "headless")) out = OUT_HEADLESS;
    else if ((!want || !strcmp(want, "fb")) && open_fb()) out = OUT_FB;
    else if ((!want || !strcmp(want, "drm")) && open_drm()) out = OUT_DRM;
    else return false;
    if (out != OUT_HEADLESS) {
        layout();
        fprintf(stderr, "display: %s %dx%d, %d bpp, scale %d\n", out == OUT_FB ? "fbdev" : "drm", xres, yres, bytes * 8, scale);
    }
    return true;
}

void display_present(const uint32_t *px)
{
    if (out != OUT_FB && out != OUT_DRM) return;
    int rw = cw * scale;
    if (rw * bytes > (int)sizeof line) rw = sizeof line / bytes;
    bool native = bytes == 4 && ro == 16 && go == 8 && bo == 0;
    for (int y = 0; y < ch && oy + (y + 1) * scale <= yres; y++) {
        const uint32_t *src = px + y * cw;
        for (int x = 0; x < rw; x++) {
            uint32_t c = src[x / scale];
            if (bytes == 4) ((uint32_t *)line)[x] = native ? c : ((c >> 16 & 255) << ro) | ((c >> 8 & 255) << go) | ((c & 255) << bo);
            else ((uint16_t *)line)[x] = (uint16_t)(((c >> 19 & 31) << 11) | ((c >> 10 & 63) << 5) | (c >> 3 & 31));
        }
        for (int s = 0; s < scale; s++) memcpy(base + (size_t)(oy + y * scale + s) * pitch + (size_t)ox * bytes, line, (size_t)rw * bytes);
    }
}

void display_close(void)
{
    if (base) memset(base, 0, (size_t)pitch * yres);
    if (out == OUT_DRM) {
        if (saved_crtc.mode_valid) {
            saved_crtc.set_connectors_ptr = (uintptr_t)&drm_conn;
            saved_crtc.count_connectors = 1;
            ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &saved_crtc);
        }
        ioctl(fd, DRM_IOCTL_MODE_RMFB, &drm_fb);
        struct drm_mode_destroy_dumb dd = { drm_handle };
        if (mem) munmap(mem, size);
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
    } else if (mem) munmap(mem, size);
    if (tty >= 0) { ioctl(tty, KDSETMODE, KD_TEXT); close(tty); }
    if (fd >= 0) close(fd);
    fd = tty = -1;
    mem = base = NULL;
    out = OUT_NONE;
}
#endif
