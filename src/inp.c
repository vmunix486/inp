/* inp.c -- INP image format decoder library
 * Parses INI-based INP files and decodes them into RGBA pixel buffers.
 * Pure C89: all declarations sit at the top of their block.
 */

#include "inp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SEC_NONE, SEC_GENERAL, SEC_PICTURE, SEC_PALETTE };

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_byte(const char *s)
{
    int hi, lo;
    hi = hex_digit(s[0]);
    lo = hex_digit(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

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

static int parse_pixel(const char *hex, int color_mode, const InpHeader *hdr,
                       Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    int len, idx;

    len = strlen(hex);

    switch (color_mode) {
    case INP_MODE_16BIT:
        return parse_color(hex, hdr->alpha, r, g, b, a);

    case INP_MODE_256COLOR:
    case INP_MODE_16COLOR:
        if (len != 2) return -1;
        idx = hex_byte(hex);
        if (idx < 0 || idx >= hdr->palette_count) return -1;
        *r = hdr->palette[idx][0];
        *g = hdr->palette[idx][1];
        *b = hdr->palette[idx][2];
        *a = 255;
        return 0;

    case INP_MODE_256GRAY:
    case INP_MODE_16GRAY:
        if (len != 2) return -1;
        idx = hex_byte(hex);
        if (idx < 0) return -1;
        *r = (Uint8)idx;
        *g = (Uint8)idx;
        *b = (Uint8)idx;
        *a = 255;
        return 0;

    case INP_MODE_BW:
        if (len != 1) return -1;
        if (hex[0] == '0') { *r = *g = *b = 0; *a = 255; return 0; }
        if (hex[0] == '1') { *r = *g = *b = 255; *a = 255; return 0; }
        return -1;
    }

    return -1;
}

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

static void chomp(char *s)
{
    size_t l;
    l = strlen(s);
    while (l > 0 && (s[l - 1] == '\n' || s[l - 1] == '\r')) {
        s[l - 1] = '\0';
        l--;
    }
}

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

/* ------------------------------------------------------------------ */
/* INI section parsers                                                 */
/* ------------------------------------------------------------------ */

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
                    if (strcmp(eq + 1, "16bit") == 0) hdr->colors = INP_MODE_16BIT;
                    else if (strcmp(eq + 1, "256color") == 0) hdr->colors = INP_MODE_256COLOR;
                    else if (strcmp(eq + 1, "256gray") == 0) hdr->colors = INP_MODE_256GRAY;
                    else if (strcmp(eq + 1, "16color") == 0) hdr->colors = INP_MODE_16COLOR;
                    else if (strcmp(eq + 1, "16gray") == 0) hdr->colors = INP_MODE_16GRAY;
                    else if (strcmp(eq + 1, "bw") == 0) hdr->colors = INP_MODE_BW;
                }
            }
        }
        tok = strtok(NULL, "\n");
    }

    free(copy);
}

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

#ifndef __EMSCRIPTEN__
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
#endif /* !__EMSCRIPTEN__ */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

InpImage inp_load(const char *path)
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
#ifndef __EMSCRIPTEN__
        const char *cmd = decompress_cmd(path);
        if (cmd) {
            char cmdbuf[1024];
            snprintf(cmdbuf, sizeof(cmdbuf), "%s '%s'", cmd, path);
            fp = popen(cmdbuf, "r");
            if (!fp) {
                fprintf(stderr, "inp: could not run %s on %s\n", cmd, path);
                return img;
            }
        } else
#endif /* !__EMSCRIPTEN__ */
        {
            fp = fopen(path, "rb");
            if (!fp) {
                fprintf(stderr, "inp: could not open %s\n", path);
                return img;
            }
        }
    }

    data = slurp(fp, &data_len);
    if (fp == stdin) { /* leave stdin open */ }
#ifdef __EMSCRIPTEN__
    else fclose(fp);
#else
    else if (decompress_cmd(path)) pclose(fp);
    else fclose(fp);
#endif /* __EMSCRIPTEN__ */
    if (!data) {
        fprintf(stderr, "inp: could not read %s\n", path);
        return img;
    }

    img.hdr.width = 0;
    img.hdr.height = 0;
    img.hdr.alpha = 0;
    img.hdr.colors = INP_MODE_16BIT;
    img.hdr.bg_r = 255; img.hdr.bg_g = 0; img.hdr.bg_b = 255; img.hdr.bg_a = 255;
    img.hdr.palette_count = 0;
    have_bg = 0;

    init_vga_palette(img.hdr.palette);
    scan_general(data, data_len, &img.hdr, &have_bg);
    scan_palette(data, data_len, &img.hdr);

    if (img.hdr.colors == INP_MODE_16COLOR && img.hdr.palette_count < 16) {
        img.hdr.palette_count = 16;
    }
    if (img.hdr.colors == INP_MODE_256COLOR && img.hdr.palette_count < 256) {
        img.hdr.palette_count = 256;
    }

    if (img.hdr.width <= 0 || img.hdr.height <= 0) {
        fprintf(stderr, "inp: %s: invalid or missing width/height\n", path);
        free(data);
        return img;
    }

    img.pixels = (Uint8 *)malloc((size_t)img.hdr.width * (size_t)img.hdr.height * 4);
    if (!img.pixels) {
        fprintf(stderr, "inp: out of memory\n");
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
        fprintf(stderr, "inp: warning: %s: %d missing pixel(s), filled with %s\n",
                path, missing, have_bg ? "the configured background color" : "magenta");
    }

    img.width = img.hdr.width;
    img.height = img.hdr.height;
    img.ok = 1;
    return img;
}

void inp_free(InpImage *img)
{
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}
