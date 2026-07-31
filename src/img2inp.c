/* img2inp.c -- converts PNG/JPG/BMP/etc. into the INP image format.
 * Spec:  https://slow.c2dthinkcentre.com/articles/inp.html
 * Uses stb_image.h (https://github.com/nothings/stb) for decoding, so it
 * reads whatever stb_image supports: PNG, JPEG, BMP, TGA, GIF, PSD, PIC.
 *
 * Build: gcc -O2 -o img2inp img2inp.c -lm
 * Usage: img2inp input.png output.inp [options]
 *
 * Options:
 *   --alpha              Force alpha channel on, even if the source has none.
 *   --no-alpha           Force alpha channel off (flattens onto white).
 *   --background=RRGGBB  Set the [General] background key to this color,
 *                         AND skip writing any pixel that exactly matches
 *                         it. Cuts file size for images with large flat
 *                         areas, at the cost of those pixels reading back
 *                         as "background" rather than an explicit value.
 *   --name=STRING         Sets General.name
 *   --creator=STRING      Sets General.creator
 *   --note=STRING         Sets General.note
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

int main(int argc, char *argv[])
{
    const char *in_path, *out_path;
    const char *bg_str, *name, *creator, *note;
    int force_alpha, force_no_alpha, have_bg;
    unsigned char bg_r, bg_g, bg_b;
    int width, height, channels;
    unsigned char *img;
    int use_alpha;
    long pixel_count;
    FILE *out;
    int x, y;
    int skipped;

    if (argc < 3) { usage(argv[0]); return 1; }

    in_path = argv[1];
    out_path = argv[2];
    force_alpha = 0;
    force_no_alpha = 0;
    bg_str = NULL;
    name = NULL;
    creator = NULL;
    note = NULL;

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
    if (have_bg) fprintf(out, "background=%02x%02x%02x\n", bg_r, bg_g, bg_b);
    if (name) fprintf(out, "name=%s\n", name);
    if (creator) fprintf(out, "creator=%s\n", creator);
    if (note) fprintf(out, "note=%s\n", note);
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

            if (use_alpha) {
                fprintf(out, "x%dy%d=%02x%02x%02x%02x\n", x, y, r, g, b, a);
            } else {
                fprintf(out, "x%dy%d=%02x%02x%02x\n", x, y, r, g, b);
            }
        }
    }

    fclose(out);
    stbi_image_free(img);

    fprintf(stderr, "img2inp: wrote %s (%dx%d, %s)%s\n",
            out_path, width, height, use_alpha ? "RGBA" : "RGB",
            skipped > 0 ? "" : "");
    if (skipped > 0) {
        fprintf(stderr, "img2inp: skipped %d pixel(s) matching --background\n", skipped);
    }

    return 0;
}
