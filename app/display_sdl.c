#ifdef USE_SDL
#include <SDL.h>
#include "display.h"
#include "events.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static int cw, ch;

bool display_open(int w, int h)
{
    cw = w;
    ch = h;
    if (SDL_Init(SDL_INIT_VIDEO)) return false;
    win = SDL_CreateWindow("Motion Ski", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * 2, h * 2, SDL_WINDOW_RESIZABLE);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h) : NULL;
    if (!tex) return false;
    SDL_RenderSetLogicalSize(ren, w, h);
    return true;
}

static void key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_LEFT: case SDLK_a: events_push(EV_LEFT, SRC_KEYBOARD); break;
    case SDLK_RIGHT: case SDLK_d: events_push(EV_RIGHT, SRC_KEYBOARD); break;
    case SDLK_UP: events_push(EV_UP, SRC_KEYBOARD); break;
    case SDLK_DOWN: events_push(EV_DOWN, SRC_KEYBOARD); break;
    case SDLK_w: case SDLK_SPACE: events_push(EV_JUMP, SRC_KEYBOARD); break;
    case SDLK_s: events_push(EV_CROUCH, SRC_KEYBOARD); break;
    case SDLK_RETURN: events_push(EV_SELECT, SRC_KEYBOARD); break;
    case SDLK_ESCAPE: case SDLK_q: case SDLK_BACKSPACE: events_push(EV_BACK, SRC_KEYBOARD); break;
    case SDLK_p: events_push(EV_SCREENSHOT, SRC_KEYBOARD); break;
    }
}

void display_present(const uint32_t *px)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_running = false;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) key(e.key.keysym.sym);
    }
    SDL_UpdateTexture(tex, NULL, px, cw * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void display_close(void)
{
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}
#endif
