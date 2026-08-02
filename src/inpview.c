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

/* Sets the window title to "inpview: path (3/15)". */
static void update_title(SDL_Window *win, const char *path, int idx, int total)
{
    char title[512];
    if (total > 1)
        snprintf(title, sizeof(title), "inpview: %s (%d/%d)", path, idx + 1, total);
    else
        snprintf(title, sizeof(title), "inpview: %s", path);
    SDL_SetWindowTitle(win, title);
}

/* Load a new image, swap out the texture, free old resources. */
static int switch_image(const char *path, SDL_Renderer *ren, InpImage *img,
                        SDL_Texture **tex, float *zoom, float *pan_x, float *pan_y)
{
    InpImage next;

    next = inp_load(path);
    if (!next.ok) return 0;

    SDL_DestroyTexture(*tex);
    inp_free(img);
    *img = next;

    *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, img->width, img->height);
    if (!*tex) {
        fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 0;
    }

    SDL_UpdateTexture(*tex, NULL, img->pixels, img->width * 4);
    inp_free(img);

    *zoom = 1.0f;
    *pan_x = 0.0f;
    *pan_y = 0.0f;
    return 1;
}

int main(int argc, char *argv[])
{
    int file_count, slideshow_ms, cur;
    const char *files[4096];
    InpImage img;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int running, dragging, drag_x, drag_y;
    float zoom, pan_x, pan_y;
    Uint32 last_advance;
    int i;

    file_count = 0;
    slideshow_ms = 0;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--slideshow=", 12) == 0) {
            slideshow_ms = atoi(argv[i] + 12) * 1000;
            if (slideshow_ms < 0) slideshow_ms = 0;
        } else {
            if (file_count >= 4096) {
                fprintf(stderr, "inpview: too many files\n");
                return 1;
            }
            files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        fprintf(stderr, "usage: %s [--slideshow=N] <files...>\n", argv[0]);
        return 1;
    }

    img = inp_load(files[0]);
    if (!img.ok) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "inpview: SDL_Init failed: %s\n", SDL_GetError());
        inp_free(&img);
        return 1;
    }

    win = SDL_CreateWindow("inpview",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            img.width, img.height, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "inpview: SDL_CreateWindow failed: %s\n", SDL_GetError());
        inp_free(&img);
        SDL_Quit();
        return 1;
    }

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "inpview: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        inp_free(&img);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!tex) {
        fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
        inp_free(&img);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_UpdateTexture(tex, NULL, img.pixels, img.width * 4);
    inp_free(&img);

    update_title(win, files[0], 0, file_count);

    zoom = 1.0f;
    pan_x = 0.0f;
    pan_y = 0.0f;
    dragging = 0;
    drag_x = 0;
    drag_y = 0;
    cur = 0;
    last_advance = SDL_GetTicks();
    running = 1;

    while (running) {
        int win_w, win_h;
        int base_w, base_h;
        SDL_Event ev;

        SDL_GetRendererOutputSize(ren, &win_w, &win_h);

        if ((float)img.width / img.height > (float)win_w / win_h) {
            base_w = win_w;
            base_h = (int)((float)win_w * img.height / img.width);
        } else {
            base_h = win_h;
            base_w = (int)((float)win_h * img.width / img.height);
        }

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11) {
                Uint32 flags = SDL_GetWindowFlags(win);
                if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                    SDL_SetWindowFullscreen(win, 0);
                else
                    SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_0) {
                zoom = 1.0f;
                pan_x = 0.0f;
                pan_y = 0.0f;
            }

            /* gallery navigation */
            if (ev.type == SDL_KEYDOWN && file_count > 1) {
                int want = -1;
                if (ev.key.keysym.sym == SDLK_RIGHT || ev.key.keysym.sym == SDLK_SPACE)
                    want = (cur + 1) % file_count;
                else if (ev.key.keysym.sym == SDLK_LEFT)
                    want = (cur - 1 + file_count) % file_count;
                else if (ev.key.keysym.sym == SDLK_HOME)
                    want = 0;
                else if (ev.key.keysym.sym == SDLK_END)
                    want = file_count - 1;

                if (want >= 0 && want != cur) {
                    if (switch_image(files[want], ren, &img, &tex,
                                     &zoom, &pan_x, &pan_y)) {
                        cur = want;
                        last_advance = SDL_GetTicks();
                        update_title(win, files[cur], cur, file_count);
                    }
                }
            }

            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = 1;
                drag_x = ev.button.x;
                drag_y = ev.button.y;
            }
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = 0;
            }
            if (ev.type == SDL_MOUSEMOTION && dragging) {
                pan_x += (float)(ev.motion.x - drag_x);
                pan_y += (float)(ev.motion.y - drag_y);
                drag_x = ev.motion.x;
                drag_y = ev.motion.y;
            }

            if (ev.type == SDL_MOUSEWHEEL || ev.type == SDL_KEYDOWN) {
                float new_zoom;
                int mx, my;
                int dst_w_now, dst_h_now;
                int base_x, base_y;
                float img_fx, img_fy;
                int new_dst_w, new_dst_h, new_base_x, new_base_y;

                if (ev.type == SDL_MOUSEWHEEL) {
                    new_zoom = (ev.wheel.y > 0) ? zoom * 1.1f : zoom / 1.1f;
                    SDL_GetMouseState(&mx, &my);
                } else if (ev.key.keysym.sym == SDLK_EQUALS) {
                    new_zoom = zoom * 1.25f;
                    mx = win_w / 2;
                    my = win_h / 2;
                } else if (ev.key.keysym.sym == SDLK_MINUS) {
                    new_zoom = zoom / 1.25f;
                    mx = win_w / 2;
                    my = win_h / 2;
                } else {
                    continue;
                }

                if (new_zoom < 0.05f) new_zoom = 0.05f;
                if (new_zoom > 50.0f) new_zoom = 50.0f;

                dst_w_now = (int)((float)base_w * zoom);
                dst_h_now = (int)((float)base_h * zoom);
                base_x = (win_w - dst_w_now) / 2;
                base_y = (win_h - dst_h_now) / 2;

                img_fx = (float)(mx - base_x - (int)pan_x) / (float)dst_w_now;
                img_fy = (float)(my - base_y - (int)pan_y) / (float)dst_h_now;

                new_dst_w = (int)((float)base_w * new_zoom);
                new_dst_h = (int)((float)base_h * new_zoom);
                new_base_x = (win_w - new_dst_w) / 2;
                new_base_y = (win_h - new_dst_h) / 2;

                pan_x = (float)(mx - new_base_x) - img_fx * (float)new_dst_w;
                pan_y = (float)(my - new_base_y) - img_fy * (float)new_dst_h;

                zoom = new_zoom;
            }
        }

        /* slideshow auto-advance */
        if (slideshow_ms > 0 && file_count > 1 && running) {
            Uint32 now = SDL_GetTicks();
            if (now - last_advance >= (Uint32)slideshow_ms) {
                int want = (cur + 1) % file_count;
                if (switch_image(files[want], ren, &img, &tex,
                                 &zoom, &pan_x, &pan_y)) {
                    cur = want;
                    last_advance = now;
                    update_title(win, files[cur], cur, file_count);
                } else {
                    last_advance = now;
                }
            }
        }

        {
            int dst_w, dst_h;
            SDL_Rect dst;

            dst_w = (int)((float)base_w * zoom);
            dst_h = (int)((float)base_h * zoom);

            dst.x = (win_w - dst_w) / 2 + (int)pan_x;
            dst.y = (win_h - dst_h) / 2 + (int)pan_y;
            dst.w = dst_w;
            dst.h = dst_h;

            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, &dst);
            SDL_RenderPresent(ren);
        }
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}
