/* img2inp.c -- converts PNG/JPG/BMP/etc. into the INP image format.
 * Spec:  https://slow.c2dthinkcentre.com/articles/inp.html
 * Uses stb_image.h (https://github.com/nothings/stb) for decoding, so it
 * reads whatever stb_image supports: PNG, JPEG, BMP, TGA, GIF, PSD, PIC.
 *
 * Usage: img2inp input.png output.inp [options]
*/

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s <input> <output.inp> [options]\n"
        "  --alpha                 force alpha channel on\n"
        "  --no-alpha              force alpha channel off\n"
        "  --background=RRGGBB     set background + skip matching pixels\n"
        "  --colors=MODE           set color mode (16bit, 256color, 256gray,\n"
        "                             16color, 16gray, bw)\n"
        "  --extract-palette       extract most common colors from image\n"
        "                          (used with 256color/16color) (experimental)\n"
        "  --name=STRING           set General.name\n"
        "  --creator=STRING        set General.creator\n"
        "  --note=STRING           set General.note\n",
        prog);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parses "RRGGBB" into r,g,b. Returns 0 on success. */
static int parse_hex6(const char *s, unsigned char *r, unsigned char *g, unsigned char *b)
{
    int v[6];
    int i;

    if (strlen(s) != 6) return -1;
    for (i = 0; i < 6; i++) {
        v[i] = hex_digit(s[i]);
        if (v[i] < 0) return -1;
    }
    *r = (unsigned char)((v[0] << 4) | v[1]);
    *g = (unsigned char)((v[2] << 4) | v[3]);
    *b = (unsigned char)((v[4] << 4) | v[5]);
    return 0;
}

enum { MODE_16BIT, MODE_256COLOR, MODE_256GRAY, MODE_16COLOR, MODE_16GRAY, MODE_BW };

static const unsigned char vga_palette[256][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},
    {170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},
    {255,85,85},{255,85,255},{255,255,85},{255,255,255},
    {0,0,0},{0,0,95},{0,0,135},{0,0,175},
    {0,0,215},{0,0,255},{0,95,0},{0,95,95},
    {0,95,135},{0,95,175},{0,95,215},{0,95,255},
    {0,135,0},{0,135,95},{0,135,135},{0,135,175},
    {0,135,215},{0,135,255},{0,175,0},{0,175,95},
    {0,175,135},{0,175,175},{0,175,215},{0,175,255},
    {0,215,0},{0,215,95},{0,215,135},{0,215,175},
    {0,215,215},{0,215,255},{0,255,0},{0,255,95},
    {0,255,135},{0,255,175},{0,255,215},{0,255,255},
    {95,0,0},{95,0,95},{95,0,135},{95,0,175},
    {95,0,215},{95,0,255},{95,95,0},{95,95,95},
    {95,95,135},{95,95,175},{95,95,215},{95,95,255},
    {95,135,0},{95,135,95},{95,135,135},{95,135,175},
    {95,135,215},{95,135,255},{95,175,0},{95,175,95},
    {95,175,135},{95,175,175},{95,175,215},{95,175,255},
    {95,215,0},{95,215,95},{95,215,135},{95,215,175},
    {95,215,215},{95,215,255},{95,255,0},{95,255,95},
    {95,255,135},{95,255,175},{95,255,215},{95,255,255},
    {135,0,0},{135,0,95},{135,0,135},{135,0,175},
    {135,0,215},{135,0,255},{135,95,0},{135,95,95},
    {135,95,135},{135,95,175},{135,95,215},{135,95,255},
    {135,135,0},{135,135,95},{135,135,135},{135,135,175},
    {135,135,215},{135,135,255},{135,175,0},{135,175,95},
    {135,175,135},{135,175,175},{135,175,215},{135,175,255},
    {135,215,0},{135,215,95},{135,215,135},{135,215,175},
    {135,215,215},{135,215,255},{135,255,0},{135,255,95},
    {135,255,135},{135,255,175},{135,255,215},{135,255,255},
    {175,0,0},{175,0,95},{175,0,135},{175,0,175},
    {175,0,215},{175,0,255},{175,95,0},{175,95,95},
    {175,95,135},{175,95,175},{175,95,215},{175,95,255},
    {175,135,0},{175,135,95},{175,135,135},{175,135,175},
    {175,135,215},{175,135,255},{175,175,0},{175,175,95},
    {175,175,135},{175,175,175},{175,175,215},{175,175,255},
    {175,215,0},{175,215,95},{175,215,135},{175,215,175},
    {175,215,215},{175,215,255},{175,255,0},{175,255,95},
    {175,255,135},{175,255,175},{175,255,215},{175,255,255},
    {215,0,0},{215,0,95},{215,0,135},{215,0,175},
    {215,0,215},{215,0,255},{215,95,0},{215,95,95},
    {215,95,135},{215,95,175},{215,95,215},{215,95,255},
    {215,135,0},{215,135,95},{215,135,135},{215,135,175},
    {215,135,215},{215,135,255},{215,175,0},{215,175,95},
    {215,175,135},{215,175,175},{215,175,215},{215,175,255},
    {215,215,0},{215,215,95},{215,215,135},{215,215,175},
    {215,215,215},{215,215,255},{215,255,0},{215,255,95},
    {215,255,135},{215,255,175},{215,255,215},{215,255,255},
    {255,0,0},{255,0,95},{255,0,135},{255,0,175},
    {255,0,215},{255,0,255},{255,95,0},{255,95,95},
    {255,95,135},{255,95,175},{255,95,215},{255,95,255},
    {255,135,0},{255,135,95},{255,135,135},{255,135,175},
    {255,135,215},{255,135,255},{255,175,0},{255,175,95},
    {255,175,135},{255,175,175},{255,175,215},{255,175,255},
    {255,215,0},{255,215,95},{255,215,135},{255,215,175},
    {255,215,215},{255,215,255},{255,255,0},{255,255,95},
    {255,255,135},{255,255,175},{255,255,215},{255,255,255},
    {8,8,8},{18,18,18},{28,28,28},{38,38,38},
    {48,48,48},{58,58,58},{68,68,68},{78,78,78},
    {88,88,88},{98,98,98},{108,108,108},{118,118,118},
    {128,128,128},{138,138,138},{148,148,148},{158,158,158},
    {168,168,168},{178,178,178},{188,188,188},{198,198,198},
    {208,208,208},{218,218,218},{228,228,228},{238,238,238}
};

#define HTAB_SIZE 65536
#define HTAB_MASK (HTAB_SIZE - 1)

typedef struct {
    unsigned int color;
    int count;
    int used;
} HEntry;

static HEntry htab[HTAB_SIZE];

static unsigned int color_hash(unsigned int c)
{
    c = ((c >> 16) ^ c) * 0x45d9f3b;
    c = ((c >> 16) ^ c);
    return c & HTAB_MASK;
}

static void htab_insert(unsigned int color)
{
    unsigned int h = color_hash(color);
    while (htab[h].used && htab[h].color != color) {
        h = (h + 1) & HTAB_MASK;
    }
    if (!htab[h].used) {
        htab[h].color = color;
        htab[h].used = 1;
        htab[h].count = 1;
    } else {
        htab[h].count++;
    }
}

typedef struct {
    unsigned int color;
    int count;
} ColorCount;

static int compare_counts(const void *a, const void *b)
{
    const ColorCount *ca = (const ColorCount *)a;
    const ColorCount *cb = (const ColorCount *)b;
    return cb->count - ca->count;
}

static int extract_palette(unsigned char *img, int w, int h,
                           unsigned char out_palette[][3], int max_colors)
{
    int i, n = w * h;
    int total = 0;
    ColorCount *counts;

    counts = (ColorCount *)malloc(HTAB_SIZE * sizeof(ColorCount));
    if (!counts) return 0;

    memset(htab, 0, sizeof(htab));

    for (i = 0; i < n; i++) {
        unsigned int c = ((unsigned int)img[i*4+0] << 24) |
                         ((unsigned int)img[i*4+1] << 16) |
                         ((unsigned int)img[i*4+2] << 8) |
                         (unsigned int)img[i*4+3];
        htab_insert(c);
    }

    for (i = 0; i < HTAB_SIZE; i++) {
        if (htab[i].used && total < HTAB_SIZE) {
            counts[total].color = htab[i].color;
            counts[total].count = htab[i].count;
            total++;
        }
    }

    qsort(counts, total, sizeof(ColorCount), compare_counts);

    if (total > max_colors) total = max_colors;
    for (i = 0; i < total; i++) {
        out_palette[i][0] = (counts[i].color >> 24) & 0xff;
        out_palette[i][1] = (counts[i].color >> 16) & 0xff;
        out_palette[i][2] = (counts[i].color >> 8) & 0xff;
    }

    free(counts);
    return total;
}

static int find_nearest(int r, int g, int b,
                        const unsigned char palette[][3], int count)
{
    int best = 0, best_d = 256*256*3, i;
    for (i = 0; i < count; i++) {
        int dr = r - palette[i][0];
        int dg = g - palette[i][1];
        int db = b - palette[i][2];
        int d = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static void clamp_pixel(int *v)
{
    if (*v < 0) *v = 0;
    if (*v > 255) *v = 255;
}

static void dither_palette(unsigned char *img, int w, int h,
                           const unsigned char palette[][3], int pal_count)
{
    int x, y, ch;
    int *buf = (int *)malloc((size_t)w * (size_t)h * 3 * sizeof(int));
    if (!buf) return;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            int di = (y * w + x) * 3;
            buf[di+0] = img[si+0]; buf[di+1] = img[si+1]; buf[di+2] = img[si+2];
        }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int bi = (y * w + x) * 3;
            int si = (y * w + x) * 4;
            int old[3], ni, err[3];

            for (ch = 0; ch < 3; ch++) { old[ch] = buf[bi+ch]; clamp_pixel(&old[ch]); }
            ni = find_nearest(old[0], old[1], old[2], palette, pal_count);
            for (ch = 0; ch < 3; ch++) {
                err[ch] = old[ch] - (int)palette[ni][ch];
                buf[bi+ch] = (int)palette[ni][ch];
                img[si+ch] = palette[ni][ch];
            }

            if (x+1 < w) { int i=bi+3; buf[i+0]+=err[0]*7/16; buf[i+1]+=err[1]*7/16; buf[i+2]+=err[2]*7/16; }
            if (y+1 < h) {
                if (x>0) { int i=bi+w*3-3; buf[i+0]+=err[0]*3/16; buf[i+1]+=err[1]*3/16; buf[i+2]+=err[2]*3/16; }
                { int i=bi+w*3; buf[i+0]+=err[0]*5/16; buf[i+1]+=err[1]*5/16; buf[i+2]+=err[2]*5/16; }
                if (x+1<w) { int i=bi+w*3+3; buf[i+0]+=err[0]/16; buf[i+1]+=err[1]/16; buf[i+2]+=err[2]/16; }
            }
        }

    free(buf);
}

static void dither_grayscale(unsigned char *img, int w, int h, int levels)
{
    int x, y;
    int *buf = (int *)malloc((size_t)w * (size_t)h * sizeof(int));
    if (!buf) return;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            buf[y*w+x] = (int)(0.299*img[si+0] + 0.587*img[si+1] + 0.114*img[si+2]);
        }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int bi = y*w+x;
            int si = bi*4;
            int old_val, new_val, err;

            old_val = buf[bi]; clamp_pixel(&old_val);
            new_val = old_val * (levels-1) / 255;
            new_val = new_val * 255 / (levels-1);
            err = old_val - new_val;
            buf[bi] = new_val;
            img[si+0] = (unsigned char)new_val;
            img[si+1] = (unsigned char)new_val;
            img[si+2] = (unsigned char)new_val;

            if (x+1<w) buf[bi+1] += err*7/16;
            if (y+1<h) {
                if (x>0) buf[bi+w-1] += err*3/16;
                buf[bi+w] += err*5/16;
                if (x+1<w) buf[bi+w+1] += err/16;
            }
        }

    free(buf);
}

static void convert_bw(unsigned char *img, int w, int h)
{
    int i, n = w * h;
    for (i = 0; i < n; i++) {
        int si = i * 4;
        int gray = (int)(0.299*img[si+0] + 0.587*img[si+1] + 0.114*img[si+2]);
        unsigned char val = (unsigned char)((gray < 128) ? 0 : 255);
        img[si+0] = val; img[si+1] = val; img[si+2] = val;
    }
}

int main(int argc, char *argv[])
{
    const char *in_path, *out_path;
    const char *bg_str, *name, *creator, *note, *colors_str;
    int force_alpha, force_no_alpha, have_bg, extract_palette_flag;
    unsigned char bg_r, bg_g, bg_b;
    int width, height, channels;
    unsigned char *img;
    int use_alpha, color_mode;
    long pixel_count;
    FILE *out;
    int x, y;
    int skipped;

    if (argc < 3) { usage(argv[0]); return 1; }

    in_path = argv[1];
    out_path = argv[2];
    force_alpha = 0;
    force_no_alpha = 0;
    extract_palette_flag = 0;
    bg_str = NULL;
    name = NULL;
    creator = NULL;
    note = NULL;
    colors_str = NULL;

    {
        int i;
        for (i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--alpha") == 0) {
                force_alpha = 1;
            } else if (strcmp(argv[i], "--no-alpha") == 0) {
                force_no_alpha = 1;
            } else if (strncmp(argv[i], "--background=", 13) == 0) {
                bg_str = argv[i] + 13;
            } else if (strncmp(argv[i], "--name=", 7) == 0) {
                name = argv[i] + 7;
            } else if (strncmp(argv[i], "--creator=", 10) == 0) {
                creator = argv[i] + 10;
            } else if (strncmp(argv[i], "--note=", 7) == 0) {
                note = argv[i] + 7;
            } else if (strncmp(argv[i], "--colors=", 9) == 0) {
                colors_str = argv[i] + 9;
            } else if (strcmp(argv[i], "--extract-palette") == 0) {
                extract_palette_flag = 1;
            } else {
                fprintf(stderr, "img2inp: unknown option %s\n", argv[i]);
                usage(argv[0]);
                return 1;
            }
        }
    }

    have_bg = 0;
    bg_r = bg_g = bg_b = 0;
    if (bg_str) {
        if (parse_hex6(bg_str, &bg_r, &bg_g, &bg_b) != 0) {
            fprintf(stderr, "img2inp: --background must be 6 hex digits (RRGGBB)\n");
            return 1;
        }
        have_bg = 1;
    }

    img = stbi_load(in_path, &width, &height, &channels, 4); /* always decode as RGBA */
    if (!img) {
        fprintf(stderr, "img2inp: could not load %s: %s\n", in_path, stbi_failure_reason());
        return 1;
    }

    /* Decide whether to emit alpha. Auto: on if the source had an alpha
     * channel AND any pixel is not fully opaque. */
    use_alpha = 0;
    if (!force_no_alpha) {
        if (force_alpha) {
            use_alpha = 1;
        } else if (channels == 4) {
            long i, n;
            n = (long)width * (long)height;
            for (i = 0; i < n; i++) {
                if (img[i * 4 + 3] != 255) { use_alpha = 1; break; }
            }
        }
    }

    color_mode = MODE_16BIT;
    if (colors_str) {
        if (strcmp(colors_str, "16bit") == 0) color_mode = MODE_16BIT;
        else if (strcmp(colors_str, "256color") == 0) color_mode = MODE_256COLOR;
        else if (strcmp(colors_str, "256gray") == 0) color_mode = MODE_256GRAY;
        else if (strcmp(colors_str, "16color") == 0) color_mode = MODE_16COLOR;
        else if (strcmp(colors_str, "16gray") == 0) color_mode = MODE_16GRAY;
        else if (strcmp(colors_str, "bw") == 0) color_mode = MODE_BW;
        else {
            fprintf(stderr, "img2inp: unknown color mode '%s'\n", colors_str);
            stbi_image_free(img);
            return 1;
        }
    }

    if (color_mode != MODE_16BIT && use_alpha) {
        fprintf(stderr, "img2inp: warning: alpha not supported in this color mode, ignoring alpha\n");
        use_alpha = 0;
    }

    {
        const unsigned char (*pal)[3] = vga_palette;
        int pal_count;
        unsigned char ext_pal[256][3];

        pal_count = (color_mode == MODE_16COLOR) ? 16 : 256;

        if (extract_palette_flag && (color_mode == MODE_256COLOR || color_mode == MODE_16COLOR)) {
            int got = extract_palette(img, width, height, ext_pal, pal_count);
            fprintf(stderr, "img2inp: extracted %d colors from image\n", got);
            pal = ext_pal;
            pal_count = got;
        }

        if (color_mode == MODE_256COLOR || color_mode == MODE_16COLOR) {
            dither_palette(img, width, height, pal, pal_count);
        } else if (color_mode == MODE_256GRAY) {
            dither_grayscale(img, width, height, 256);
        } else if (color_mode == MODE_16GRAY) {
            dither_grayscale(img, width, height, 16);
        } else if (color_mode == MODE_BW) {
            convert_bw(img, width, height);
        }

        pixel_count = (long)width * (long)height;
        if (pixel_count > 200000) {
            fprintf(stderr,
                "img2inp: warning: %ldx%ld = %ld pixels -> the .inp file will have "
                "roughly that many lines. Consider piping the output through xz or "
                "gzip (INP has no built-in compression).\n",
                (long)width, (long)height, pixel_count);
        }

        out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "img2inp: could not open %s for writing\n", out_path);
            stbi_image_free(img);
            return 1;
        }

        fprintf(out, "[General]\n");
        fprintf(out, "width=%d\n", width);
        fprintf(out, "height=%d\n", height);
        if (use_alpha) fprintf(out, "alpha=1\n");
        if (color_mode == MODE_256COLOR) fprintf(out, "colors=256color\n");
        else if (color_mode == MODE_256GRAY) fprintf(out, "colors=256gray\n");
        else if (color_mode == MODE_16COLOR) fprintf(out, "colors=16color\n");
        else if (color_mode == MODE_16GRAY) fprintf(out, "colors=16gray\n");
        else if (color_mode == MODE_BW) fprintf(out, "colors=bw\n");
        if (have_bg) fprintf(out, "background=%02x%02x%02x\n", bg_r, bg_g, bg_b);
        if (name) fprintf(out, "name=%s\n", name);
        if (creator) fprintf(out, "creator=%s\n", creator);
        if (note) fprintf(out, "note=%s\n", note);

        if (color_mode == MODE_256COLOR || color_mode == MODE_16COLOR) {
            int pi;
            fprintf(out, "\n[Palette]\n");
            for (pi = 0; pi < pal_count; pi++) {
                fprintf(out, "c%d=%02x%02x%02x\n", pi,
                        pal[pi][0], pal[pi][1], pal[pi][2]);
            }
        }

        fprintf(out, "\n[Picture]\n");

        skipped = 0;
        for (y = 1; y <= height; y++) {
            for (x = 1; x <= width; x++) {
                long idx;
                unsigned char r, g, b, a;

                idx = ((long)(y - 1) * width + (x - 1)) * 4;
                r = img[idx + 0];
                g = img[idx + 1];
                b = img[idx + 2];
                a = img[idx + 3];

                if (have_bg && r == bg_r && g == bg_g && b == bg_b
                    && (!use_alpha || a == 255)) {
                    skipped++;
                    continue;
                }

                switch (color_mode) {
                case MODE_256COLOR:
                case MODE_16COLOR: {
                    int ci = find_nearest(r, g, b, pal, pal_count);
                    fprintf(out, "x%dy%d=%02x\n", x, y, ci);
                    break;
                }
                case MODE_256GRAY:
                case MODE_16GRAY:
                    fprintf(out, "x%dy%d=%02x\n", x, y, r);
                    break;
                case MODE_BW:
                    fprintf(out, "x%dy%d=%c\n", x, y, (r < 128) ? '0' : '1');
                    break;
                default:
                    if (use_alpha) {
                        fprintf(out, "x%dy%d=%02x%02x%02x%02x\n", x, y, r, g, b, a);
                    } else {
                        fprintf(out, "x%dy%d=%02x%02x%02x\n", x, y, r, g, b);
                    }
                    break;
                }
            }
        }

        fclose(out);

        fprintf(stderr, "img2inp: wrote %s (%dx%d, %s", out_path, width, height,
                (color_mode == MODE_256COLOR) ? "256color" :
                (color_mode == MODE_256GRAY) ? "256gray" :
                (color_mode == MODE_16COLOR) ? "16color" :
                (color_mode == MODE_16GRAY) ? "16gray" :
                (color_mode == MODE_BW) ? "bw" :
                (use_alpha ? "RGBA" : "RGB"));
        fprintf(stderr, ")");
        if (skipped > 0) {
            fprintf(stderr, ", skipped %d pixel(s) matching --background", skipped);
        }
        fprintf(stderr, "\n");
    }

    stbi_image_free(img);
    return 0;
}
