/* inpview.c -- reference viewer for the INP image format
 * Usage: inpview [--slideshow=N] <files...>
 *
 * This is the frontend: display only.  All INP decoding is in inp.c.
 * Written in strict C89: all declarations sit at the top of their block.
 */

#include "inp.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* JS interop: file picker and virtual FS write */
EM_JS(void, browser_show_file_picker, (), {
    if (!Module._file_picker_input) {
        var input = document.createElement('input');
        input.type = 'file';
        input.accept = '.inp,.inp.xz,.inp.gz,.inp.bz2,.inp.zst,.inp.lzma,.inp.Z';
        input.style.cssText = 'position:fixed;top:10px;left:10px;z-index:9999;' +
            'font-size:16px;padding:8px 16px;background:#222;color:#eee;' +
            'border:1px solid #555;border-radius:4px;cursor:pointer;';
        input.id = 'inp-file-picker';
        input.onchange = function(e) {
            var file = e.target.files[0];
            if (!file) return;
            var reader = new FileReader();
            reader.onload = function(ev) {
                var data = new Uint8Array(ev.target.result);
                FS.writeFile('/tmp/inpfile', data);
                Module._browser_file_picked();
            };
            reader.readAsArrayBuffer(file);
        };
        document.body.appendChild(input);
        Module._file_picker_input = input;
    }
    Module._file_picker_input.style.display = 'block';
});

EM_JS(void, browser_hide_file_picker, (), {
    if (Module._file_picker_input)
        Module._file_picker_input.style.display = 'none';
});

EMSCRIPTEN_KEEPALIVE
void browser_file_picked(void);

#endif /* __EMSCRIPTEN__ */

/* ------------------------------------------------------------------ */
/* State (global so emscripten_set_main_loop can reach it)             */
/* ------------------------------------------------------------------ */

typedef struct {
    int file_count, slideshow_ms, cur;
    const char *files[4096];
    InpImage img;
    int img_ok;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int running, dragging, drag_x, drag_y;
    float zoom, pan_x, pan_y;
    Uint32 last_advance;
#ifdef __EMSCRIPTEN__
    int pending_load;   /* 1 when browser_file_picked wrote a file to /tmp/inpfile */
#endif
} AppState;

static AppState st;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void update_title(void)
{
    char title[512];
    if (st.file_count > 1)
        snprintf(title, sizeof(title), "inpview: %s (%d/%d)",
                 st.files[st.cur], st.cur + 1, st.file_count);
    else
        snprintf(title, sizeof(title), "inpview: %s", st.files[st.cur]);
    SDL_SetWindowTitle(st.win, title);
}

static int switch_image(const char *path)
{
    InpImage next;

    next = inp_load(path);
    if (!next.ok) return 0;

    SDL_DestroyTexture(st.tex);
    inp_free(&st.img);
    st.img = next;

    st.tex = SDL_CreateTexture(st.ren, SDL_PIXELFORMAT_RGBA32,
                               SDL_TEXTUREACCESS_STATIC, st.img.width, st.img.height);
    if (!st.tex) {
        fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_UpdateTexture(st.tex, NULL, st.img.pixels, st.img.width * 4);
    inp_free(&st.img);

    st.zoom = 1.0f;
    st.pan_x = 0.0f;
    st.pan_y = 0.0f;
    return 1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
void browser_file_picked(void)
{
    st.pending_load = 1;
}
#endif

/* ------------------------------------------------------------------ */
/* Main loop (called once per frame by emscripten or the while loop)   */
/* ------------------------------------------------------------------ */

static void main_loop_iter(void)
{
    int win_w, win_h;
    int base_w, base_h;
    SDL_Event ev;

#ifdef __EMSCRIPTEN__
    if (st.pending_load) {
        st.pending_load = 0;
        browser_hide_file_picker();
        if (switch_image("/tmp/inpfile")) {
            st.files[0] = "/tmp/inpfile";
            st.file_count = 1;
            st.cur = 0;
            st.img_ok = 1;
            st.last_advance = SDL_GetTicks();
            update_title();
        } else {
            fprintf(stderr, "inpview: failed to load picked file\n");
            browser_show_file_picker();
        }
    }
#endif

    SDL_GetRendererOutputSize(st.ren, &win_w, &win_h);

    if (!st.tex) {
        /* no image loaded yet -- clear to black */
        SDL_RenderClear(st.ren);
        SDL_RenderPresent(st.ren);
#ifndef __EMSCRIPTEN__
        SDL_Delay(16);
#endif
        return;
    }

    if ((float)st.img.width / st.img.height > (float)win_w / win_h) {
        base_w = win_w;
        base_h = (int)((float)win_w * st.img.height / st.img.width);
    } else {
        base_h = win_h;
        base_w = (int)((float)win_h * st.img.width / st.img.height);
    }

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) st.running = 0;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) st.running = 0;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11) {
            Uint32 flags = SDL_GetWindowFlags(st.win);
            if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                SDL_SetWindowFullscreen(st.win, 0);
            else
                SDL_SetWindowFullscreen(st.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_0) {
            st.zoom = 1.0f;
            st.pan_x = 0.0f;
            st.pan_y = 0.0f;
        }

        /* gallery navigation */
        if (ev.type == SDL_KEYDOWN && st.file_count > 1) {
            int want = -1;
            if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_SPACE)
                want = (st.cur + 1) % st.file_count;
            else if (ev.key.keysym.sym == SDLK_LEFT)
                want = (st.cur - 1 + st.file_count) % st.file_count;
            else if (ev.key.keysym.sym == SDLK_HOME)
                want = 0;
            else if (ev.key.keysym.sym == SDLK_END)
                want = st.file_count - 1;

            if (want >= 0 && want != st.cur) {
                if (switch_image(st.files[want])) {
                    st.cur = want;
                    st.last_advance = SDL_GetTicks();
                    update_title();
                }
            }
        }

        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            st.dragging = 1;
            st.drag_x = ev.button.x;
            st.drag_y = ev.button.y;
        }
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
            st.dragging = 0;
        }
        if (ev.type == SDL_MOUSEMOTION && st.dragging) {
            st.pan_x += (float)(ev.motion.x - st.drag_x);
            st.pan_y += (float)(ev.motion.y - st.drag_y);
            st.drag_x = ev.motion.x;
            st.drag_y = ev.motion.y;
        }

        if (ev.type == SDL_MOUSEWHEEL || ev.type == SDL_KEYDOWN) {
            float new_zoom;
            int mx, my;
            int dst_w_now, dst_h_now;
            int base_x, base_y;
            float img_fx, img_fy;
            int new_dst_w, new_dst_h, new_base_x, new_base_y;

            if (ev.type == SDL_MOUSEWHEEL) {
                new_zoom = (ev.wheel.y > 0) ? st.zoom * 1.1f : st.zoom / 1.1f;
                SDL_GetMouseState(&mx, &my);
            } else if (ev.key.keysym.sym == SDLK_EQUALS) {
                new_zoom = st.zoom * 1.25f;
                mx = win_w / 2;
                my = win_h / 2;
            } else if (ev.key.keysym.sym == SDLK_MINUS) {
                new_zoom = st.zoom / 1.25f;
                mx = win_w / 2;
                my = win_h / 2;
            } else {
                continue;
            }

            if (new_zoom < 0.05f) new_zoom = 0.05f;
            if (new_zoom > 50.0f) new_zoom = 50.0f;

            dst_w_now = (int)((float)base_w * st.zoom);
            dst_h_now = (int)((float)base_h * st.zoom);
            base_x = (win_w - dst_w_now) / 2;
            base_y = (win_h - dst_h_now) / 2;

            img_fx = (float)(mx - base_x - (int)st.pan_x) / (float)dst_w_now;
            img_fy = (float)(my - base_y - (int)st.pan_y) / (float)dst_h_now;

            new_dst_w = (int)((float)base_w * new_zoom);
            new_dst_h = (int)((float)base_h * new_zoom);
            new_base_x = (win_w - new_dst_w) / 2;
            new_base_y = (win_h - new_dst_h) / 2;

            st.pan_x = (float)(mx - new_base_x) - img_fx * (float)new_dst_w;
            st.pan_y = (float)(my - new_base_y) - img_fy * (float)new_dst_h;

            st.zoom = new_zoom;
        }
    }

    /* slideshow auto-advance */
    if (st.slideshow_ms > 0 && st.file_count > 1 && st.running) {
        Uint32 now = SDL_GetTicks();
        if (now - st.last_advance >= (Uint32)st.slideshow_ms) {
            int want = (st.cur + 1) % st.file_count;
            if (switch_image(st.files[want])) {
                st.cur = want;
                st.last_advance = now;
                update_title();
            } else {
                st.last_advance = now;
            }
        }
    }

    {
        int dst_w, dst_h;
        SDL_Rect dst;

        dst_w = (int)((float)base_w * st.zoom);
        dst_h = (int)((float)base_h * st.zoom);

        dst.x = (win_w - dst_w) / 2 + (int)st.pan_x;
        dst.y = (win_h - dst_h) / 2 + (int)st.pan_y;
        dst.w = dst_w;
        dst.h = dst_h;

        SDL_RenderClear(st.ren);
        SDL_RenderCopy(st.ren, st.tex, NULL, &dst);
        SDL_RenderPresent(st.ren);
    }

#ifndef __EMSCRIPTEN__
    SDL_Delay(16);
#endif
}

#ifdef __EMSCRIPTEN__
static void em_loop(void *arg)
{
    (void)arg;
    main_loop_iter();
}
#endif

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int i;

    st.file_count = 0;
    st.slideshow_ms = 0;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--slideshow=", 12) == 0) {
            st.slideshow_ms = atoi(argv[i] + 12) * 1000;
            if (st.slideshow_ms < 0) st.slideshow_ms = 0;
        } else {
            if (st.file_count >= 4096) {
                fprintf(stderr, "inpview: too many files\n");
                return 1;
            }
            st.files[st.file_count++] = argv[i];
        }
    }

    if (st.file_count == 0) {
#ifdef __EMSCRIPTEN__
        /* Emscripten: no files passed -> show browser file picker */
        st.img_ok = 0;
#else
        fprintf(stderr, "usage: %s [--slideshow=N] <files...>\n", argv[0]);
        return 1;
#endif
    } else {
        st.img = inp_load(st.files[0]);
        if (!st.img.ok) return 1;
        st.img_ok = 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "inpview: SDL_Init failed: %s\n", SDL_GetError());
        if (st.img_ok) inp_free(&st.img);
        return 1;
    }

    st.win = SDL_CreateWindow("inpview",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              st.img_ok ? st.img.width : 640,
                              st.img_ok ? st.img.height : 480,
                              SDL_WINDOW_RESIZABLE);
    if (!st.win) {
        fprintf(stderr, "inpview: SDL_CreateWindow failed: %s\n", SDL_GetError());
        if (st.img_ok) inp_free(&st.img);
        SDL_Quit();
        return 1;
    }

    st.ren = SDL_CreateRenderer(st.win, -1, SDL_RENDERER_ACCELERATED);
    if (!st.ren) st.ren = SDL_CreateRenderer(st.win, -1, SDL_RENDERER_SOFTWARE);
    if (!st.ren) {
        fprintf(stderr, "inpview: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        if (st.img_ok) inp_free(&st.img);
        SDL_DestroyWindow(st.win);
        SDL_Quit();
        return 1;
    }

    if (st.img_ok) {
        st.tex = SDL_CreateTexture(st.ren, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STATIC, st.img.width, st.img.height);
        if (!st.tex) {
            fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
            inp_free(&st.img);
            SDL_DestroyRenderer(st.ren);
            SDL_DestroyWindow(st.win);
            SDL_Quit();
            return 1;
        }
        SDL_UpdateTexture(st.tex, NULL, st.img.pixels, st.img.width * 4);
        inp_free(&st.img);
    } else {
        st.tex = NULL;
    }

    update_title();

    st.zoom = 1.0f;
    st.pan_x = 0.0f;
    st.pan_y = 0.0f;
    st.dragging = 0;
    st.drag_x = 0;
    st.drag_y = 0;
    st.cur = 0;
    st.last_advance = SDL_GetTicks();
    st.running = 1;

#ifdef __EMSCRIPTEN__
    if (!st.img_ok) browser_show_file_picker();
    emscripten_set_main_loop_arg(em_loop, NULL, 0, 1);
#else
    while (st.running) {
        main_loop_iter();
    }

    if (st.tex) SDL_DestroyTexture(st.tex);
    SDL_DestroyRenderer(st.ren);
    SDL_DestroyWindow(st.win);
    SDL_Quit();
#endif

    return 0;
}
