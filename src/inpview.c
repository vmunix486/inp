/* inpview.c -- reference viewer for the INP image format (INI-based images)
 * Usage: inpview image.inp
 *        inpview image.inp.xz        (auto-decompresses)
 *        xzcat image.inp.xz | inpview -
 *
 * Written in strict C89: all declarations sit at the top of their block.
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SEC_NONE, SEC_GENERAL, SEC_PICTURE, SEC_PALETTE };
enum { MODE_16BIT, MODE_256COLOR, MODE_256GRAY, MODE_16COLOR, MODE_16GRAY, MODE_BW };

typedef struct {
    int width;
    int height;
    int alpha;
    int colors;
    Uint8 bg_r, bg_g, bg_b, bg_a;
    Uint8 palette[256][4];
    int palette_count;
} InpHeader;

/* Converts a single hex digit character to its numeric value, -1 if invalid. */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Converts a 2-character hex pair (e.g. "ff") to a byte value, -1 if invalid. */
static int hex_byte(const char *s)
{
    int hi, lo;
    hi = hex_digit(s[0]);
    lo = hex_digit(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

/* Parses a color string per the INP spec: 6 hex digits (RRGGBB), or if
 * alpha_enabled, 8 hex digits (RRGGBBAA). Returns 0 on success. */
static int parse_color(const char *hex, int alpha_enabled,
                        Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    size_t len;
    int rv, gv, bv, av;

    len = strlen(hex);

    if (alpha_enabled && len == 8) {
        rv = hex_byte(hex);
        gv = hex_byte(hex + 2);
        bv = hex_byte(hex + 4);
        av = hex_byte(hex + 6);
        if (rv < 0 || gv < 0 || bv < 0 || av < 0) return -1;
        *r = (Uint8)rv; *g = (Uint8)gv; *b = (Uint8)bv; *a = (Uint8)av;
        return 0;
    }

    if (len == 6) {
        rv = hex_byte(hex);
        gv = hex_byte(hex + 2);
        bv = hex_byte(hex + 4);
        if (rv < 0 || gv < 0 || bv < 0) return -1;
        *r = (Uint8)rv; *g = (Uint8)gv; *b = (Uint8)bv; *a = 255;
        return 0;
    }

    return -1;
}

/* Initializes the default VGA 256-color palette. */
static void init_vga_palette(Uint8 palette[256][4])
{
    static const Uint8 vals[6] = {0, 95, 135, 175, 215, 255};
    int i, r, g, b;

    palette[0][0] = 0;   palette[0][1] = 0;   palette[0][2] = 0;
    palette[1][0] = 0;   palette[1][1] = 0;   palette[1][2] = 170;
    palette[2][0] = 0;   palette[2][1] = 170; palette[2][2] = 0;
    palette[3][0] = 0;   palette[3][1] = 170; palette[3][2] = 170;
    palette[4][0] = 170; palette[4][1] = 0;   palette[4][2] = 0;
    palette[5][0] = 170; palette[5][1] = 0;   palette[5][2] = 170;
    palette[6][0] = 170; palette[6][1] = 85;  palette[6][2] = 0;
    palette[7][0] = 170; palette[7][1] = 170; palette[7][2] = 170;
    palette[8][0] = 85;  palette[8][1] = 85;  palette[8][2] = 85;
    palette[9][0] = 85;  palette[9][1] = 85;  palette[9][2] = 255;
    palette[10][0] = 85; palette[10][1] = 255; palette[10][2] = 85;
    palette[11][0] = 85; palette[11][1] = 255; palette[11][2] = 255;
    palette[12][0] = 255; palette[12][1] = 85; palette[12][2] = 85;
    palette[13][0] = 255; palette[13][1] = 85; palette[13][2] = 255;
    palette[14][0] = 255; palette[14][1] = 255; palette[14][2] = 85;
    palette[15][0] = 255; palette[15][1] = 255; palette[15][2] = 255;

    i = 16;
    for (r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                palette[i][0] = vals[r];
                palette[i][1] = vals[g];
                palette[i][2] = vals[b];
                i++;
            }
        }
    }

    for (i = 0; i < 24; i++) {
        Uint8 gray = (Uint8)(8 + i * 10);
        palette[232 + i][0] = gray;
        palette[232 + i][1] = gray;
        palette[232 + i][2] = gray;
    }

    for (i = 0; i < 256; i++) palette[i][3] = 255;
}

/* Parses a pixel value string based on the active color mode.
 * Returns 0 on success, -1 on invalid input. */
static int parse_pixel(const char *hex, int color_mode, const InpHeader *hdr,
                       Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    int len, idx;

    len = strlen(hex);

    switch (color_mode) {
    case MODE_16BIT:
        return parse_color(hex, hdr->alpha, r, g, b, a);

    case MODE_256COLOR:
    case MODE_16COLOR:
        if (len != 2) return -1;
        idx = hex_byte(hex);
        if (idx < 0 || idx >= hdr->palette_count) return -1;
        *r = hdr->palette[idx][0];
        *g = hdr->palette[idx][1];
        *b = hdr->palette[idx][2];
        *a = 255;
        return 0;

    case MODE_256GRAY:
    case MODE_16GRAY:
        if (len != 2) return -1;
        idx = hex_byte(hex);
        if (idx < 0) return -1;
        *r = (Uint8)idx;
        *g = (Uint8)idx;
        *b = (Uint8)idx;
        *a = 255;
        return 0;

    case MODE_BW:
        if (len != 1) return -1;
        if (hex[0] == '0') { *r = *g = *b = 0; *a = 255; return 0; }
        if (hex[0] == '1') { *r = *g = *b = 255; *a = 255; return 0; }
        return -1;
    }

    return -1;
}

/* Reads an entire stream into a malloc'd, NUL-terminated buffer.
 * Works for both regular files and pipes/stdin (non-seekable). */
static char *slurp(FILE *fp, size_t *out_len)
{
    size_t cap, len, n;
    char *buf;
    char *tmp;

    cap = 65536;
    len = 0;
    buf = (char *)malloc(cap);
    if (!buf) return NULL;

    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        n = fread(buf + len, 1, 4096, fp);
        len += n;
        if (n < 4096) {
            if (feof(fp)) break;
            if (ferror(fp)) { free(buf); return NULL; }
        }
    }

    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

/* Strips trailing \r and/or \n from a line, in place. */
static void chomp(char *s)
{
    size_t l;
    l = strlen(s);
    while (l > 0 && (s[l - 1] == '\n' || s[l - 1] == '\r')) {
        s[l - 1] = '\0';
        l--;
    }
}

/* Parses a Picture-section key of the form "x<N>y<N>" into ix, iy.
 * Returns 0 on success. */
static int parse_xy_key(const char *key, int *ix, int *iy)
{
    const char *p;
    char numbuf[32];
    int n;

    if (key[0] != 'x') return -1;
    p = key + 1;

    n = 0;
    while (*p && *p != 'y') {
        if (n >= (int)sizeof(numbuf) - 1) return -1;
        numbuf[n++] = *p++;
    }
    if (*p != 'y' || n == 0) return -1;
    numbuf[n] = '\0';
    *ix = atoi(numbuf);
    p++;

    n = 0;
    while (*p) {
        if (n >= (int)sizeof(numbuf) - 1) return -1;
        numbuf[n++] = *p++;
    }
    if (n == 0) return -1;
    numbuf[n] = '\0';
    *iy = atoi(numbuf);

    return 0;
}

/* Pass 1: scan [General] only, to learn width/height/alpha/background
 * before we know how big the pixel buffer needs to be. */
static void scan_general(const char *data, size_t data_len, InpHeader *hdr, int *have_bg)
{
    char *copy;
    char *tok;
    char *eq;
    int section;

    copy = (char *)malloc(data_len + 1);
    if (!copy) return;
    memcpy(copy, data, data_len + 1);

    section = SEC_NONE;
    tok = strtok(copy, "\n");
    while (tok) {
        chomp(tok);
        if (tok[0] == '[') {
            if (strcmp(tok, "[General]") == 0) section = SEC_GENERAL;
            else if (strcmp(tok, "[Picture]") == 0) section = SEC_PICTURE;
            else section = SEC_NONE;
        } else if (section == SEC_GENERAL) {
            eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                if (strcmp(tok, "width") == 0) {
                    hdr->width = atoi(eq + 1);
                } else if (strcmp(tok, "height") == 0) {
                    hdr->height = atoi(eq + 1);
                } else if (strcmp(tok, "alpha") == 0) {
                    hdr->alpha = (atoi(eq + 1) != 0);
                } else if (strcmp(tok, "background") == 0) {
                    Uint8 r, g, b, a;
                    if (parse_color(eq + 1, hdr->alpha, &r, &g, &b, &a) == 0) {
                        hdr->bg_r = r; hdr->bg_g = g; hdr->bg_b = b; hdr->bg_a = a;
                        *have_bg = 1;
                    }
                } else if (strcmp(tok, "colors") == 0) {
                    if (strcmp(eq + 1, "16bit") == 0) hdr->colors = MODE_16BIT;
                    else if (strcmp(eq + 1, "256color") == 0) hdr->colors = MODE_256COLOR;
                    else if (strcmp(eq + 1, "256gray") == 0) hdr->colors = MODE_256GRAY;
                    else if (strcmp(eq + 1, "16color") == 0) hdr->colors = MODE_16COLOR;
                    else if (strcmp(eq + 1, "16gray") == 0) hdr->colors = MODE_16GRAY;
                    else if (strcmp(eq + 1, "bw") == 0) hdr->colors = MODE_BW;
                }
            }
        }
        tok = strtok(NULL, "\n");
    }

    free(copy);
}

/* Pass 2b: scan [Palette] section and fill palette entries. */
static void scan_palette(const char *data, size_t data_len, InpHeader *hdr)
{
    char *copy;
    char *tok;
    char *eq;
    int section;

    copy = (char *)malloc(data_len + 1);
    if (!copy) return;
    memcpy(copy, data, data_len + 1);

    section = SEC_NONE;
    tok = strtok(copy, "\n");
    while (tok) {
        chomp(tok);
        if (tok[0] == '[') {
            if (strcmp(tok, "[Palette]") == 0) section = SEC_PALETTE;
            else section = SEC_NONE;
        } else if (section == SEC_PALETTE) {
            eq = strchr(tok, '=');
            if (eq && tok[0] == 'c') {
                int idx = atoi(tok + 1);
                if (idx >= 0 && idx < 256) {
                    Uint8 r, g, b, a;
                    if (parse_color(eq + 1, 0, &r, &g, &b, &a) == 0) {
                        hdr->palette[idx][0] = r;
                        hdr->palette[idx][1] = g;
                        hdr->palette[idx][2] = b;
                        hdr->palette[idx][3] = 255;
                        if (idx + 1 > hdr->palette_count)
                            hdr->palette_count = idx + 1;
                    }
                }
            }
        }
        tok = strtok(NULL, "\n");
    }

    free(copy);
}

/* Pass 2: scan [Picture] and fill the RGBA pixel buffer. Returns the
 * number of pixels that were never assigned a value in the file. */
static int fill_pixels(const char *data, size_t data_len, const InpHeader *hdr, Uint8 *pixels)
{
    char *copy;
    char *tok;
    char *eq;
    int section;
    int missing;

    copy = (char *)malloc(data_len + 1);
    if (!copy) return hdr->width * hdr->height;
    memcpy(copy, data, data_len + 1);

    section = SEC_NONE;
    missing = hdr->width * hdr->height;

    tok = strtok(copy, "\n");
    while (tok) {
        chomp(tok);
        if (tok[0] == '[') {
            if (strcmp(tok, "[General]") == 0) section = SEC_GENERAL;
            else if (strcmp(tok, "[Picture]") == 0) section = SEC_PICTURE;
            else section = SEC_NONE;
        } else if (section == SEC_PICTURE) {
            eq = strchr(tok, '=');
            if (eq) {
                int ix, iy;
                *eq = '\0';
                if (parse_xy_key(tok, &ix, &iy) == 0
                    && ix >= 1 && ix <= hdr->width
                    && iy >= 1 && iy <= hdr->height) {
                    Uint8 r, g, b, a;
                    if (parse_pixel(eq + 1, hdr->colors, hdr, &r, &g, &b, &a) == 0) {
                        int idx = (iy - 1) * hdr->width + (ix - 1);
                        pixels[idx * 4 + 0] = r;
                        pixels[idx * 4 + 1] = g;
                        pixels[idx * 4 + 2] = b;
                        pixels[idx * 4 + 3] = a;
                        missing--;
                    }
                }
            }
        }
        tok = strtok(NULL, "\n");
    }

    free(copy);
    return missing;
}

/* Returns the decompression command for a given file extension, or NULL
 * if the file is not recognized as compressed. */
static const char *decompress_cmd(const char *path)
{
    size_t len = strlen(path);

    if (len > 3 && strcmp(path + len - 3, ".xz") == 0)    return "xzcat";
    if (len > 3 && strcmp(path + len - 3, ".gz") == 0)    return "gzcat";
    if (len > 4 && strcmp(path + len - 4, ".bz2") == 0)   return "bzcat";
    if (len > 5 && strcmp(path + len - 5, ".lzma") == 0)  return "xzcat";
    if (len > 4 && strcmp(path + len - 4, ".zst") == 0)   return "zstdcat";
    if (len > 2 && strcmp(path + len - 2, ".Z") == 0)     return "zcat";
    return NULL;
}

/* Result of loading a single INP image. */
typedef struct {
    InpHeader hdr;
    Uint8 *pixels;
    int width, height;
    int ok;
} InpImage;

/* Opens a file (with auto-decompress), reads and decodes it into pixels.
 * Caller must free result.pixels when done. */
static InpImage load_image(const char *path)
{
    InpImage img;
    FILE *fp;
    char *data;
    size_t data_len;
    int have_bg, i, missing;

    img.ok = 0;
    img.pixels = NULL;
    img.width = 0;
    img.height = 0;

    if (strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        const char *cmd = decompress_cmd(path);
        if (cmd) {
            char cmdbuf[1024];
            snprintf(cmdbuf, sizeof(cmdbuf), "%s '%s'", cmd, path);
            fp = popen(cmdbuf, "r");
            if (!fp) {
                fprintf(stderr, "inpview: could not run %s on %s\n", cmd, path);
                return img;
            }
        } else {
            fp = fopen(path, "rb");
            if (!fp) {
                fprintf(stderr, "inpview: could not open %s\n", path);
                return img;
            }
        }
    }

    data = slurp(fp, &data_len);
    if (fp == stdin) { /* leave stdin open */ }
    else if (decompress_cmd(path)) pclose(fp);
    else fclose(fp);
    if (!data) {
        fprintf(stderr, "inpview: could not read %s\n", path);
        return img;
    }

    img.hdr.width = 0;
    img.hdr.height = 0;
    img.hdr.alpha = 0;
    img.hdr.colors = MODE_16BIT;
    img.hdr.bg_r = 255; img.hdr.bg_g = 0; img.hdr.bg_b = 255; img.hdr.bg_a = 255;
    img.hdr.palette_count = 0;
    have_bg = 0;

    init_vga_palette(img.hdr.palette);
    scan_general(data, data_len, &img.hdr, &have_bg);
    scan_palette(data, data_len, &img.hdr);

    if (img.hdr.colors == MODE_16COLOR && img.hdr.palette_count < 16) {
        img.hdr.palette_count = 16;
    }
    if (img.hdr.colors == MODE_256COLOR && img.hdr.palette_count < 256) {
        img.hdr.palette_count = 256;
    }

    if (img.hdr.width <= 0 || img.hdr.height <= 0) {
        fprintf(stderr, "inpview: %s: invalid or missing width/height\n", path);
        free(data);
        return img;
    }

    img.pixels = (Uint8 *)malloc((size_t)img.hdr.width * (size_t)img.hdr.height * 4);
    if (!img.pixels) {
        fprintf(stderr, "inpview: out of memory\n");
        free(data);
        return img;
    }
    for (i = 0; i < img.hdr.width * img.hdr.height; i++) {
        img.pixels[i * 4 + 0] = img.hdr.bg_r;
        img.pixels[i * 4 + 1] = img.hdr.bg_g;
        img.pixels[i * 4 + 2] = img.hdr.bg_b;
        img.pixels[i * 4 + 3] = img.hdr.bg_a;
    }

    missing = fill_pixels(data, data_len, &img.hdr, img.pixels);
    free(data);

    if (missing > 0) {
        fprintf(stderr, "inpview: warning: %s: %d missing pixel(s), filled with %s\n",
                path, missing, have_bg ? "the configured background color" : "magenta");
    }

    img.width = img.hdr.width;
    img.height = img.hdr.height;
    img.ok = 1;
    return img;
}

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

    img = load_image(files[0]);
    if (!img.ok) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "inpview: SDL_Init failed: %s\n", SDL_GetError());
        free(img.pixels);
        return 1;
    }

    win = SDL_CreateWindow("inpview",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            img.width, img.height, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "inpview: SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(img.pixels);
        SDL_Quit();
        return 1;
    }

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "inpview: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        free(img.pixels);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!tex) {
        fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
        free(img.pixels);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_UpdateTexture(tex, NULL, img.pixels, img.width * 4);
    free(img.pixels);
    img.pixels = NULL;

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
                    InpImage next;
                    next = load_image(files[want]);
                    if (next.ok) {
                        SDL_DestroyTexture(tex);
                        free(img.pixels);
                        img = next;
                        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                                 SDL_TEXTUREACCESS_STATIC, img.width, img.height);
                        if (!tex) {
                            fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
                            running = 0;
                            break;
                        }
                        SDL_UpdateTexture(tex, NULL, img.pixels, img.width * 4);
                        free(img.pixels);
                        img.pixels = NULL;
                        cur = want;
                        zoom = 1.0f;
                        pan_x = 0.0f;
                        pan_y = 0.0f;
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
                InpImage next;
                next = load_image(files[want]);
                if (next.ok) {
                    SDL_DestroyTexture(tex);
                    free(img.pixels);
                    img = next;
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, img.width, img.height);
                    if (!tex) {
                        fprintf(stderr, "inpview: SDL_CreateTexture failed: %s\n", SDL_GetError());
                        running = 0;
                        break;
                    }
                    SDL_UpdateTexture(tex, NULL, img.pixels, img.width * 4);
                    free(img.pixels);
                    img.pixels = NULL;
                    cur = want;
                    zoom = 1.0f;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
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
