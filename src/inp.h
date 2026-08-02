/* inp.h -- INP image format library (decoder)
 *
 * This is the backend: parses INI-based INP files and decodes them into
 * raw RGBA pixel buffers.  No display dependencies -- use with any frontend
 * (SDL2, X11, Win32, framebuffer, etc.).
 *
 * API:
 *   InpImage inp_load(const char *path);
 *     Load and decode an INP file.  Supports "-" for stdin and auto-decompression
 *     (.xz, .gz, .bz2, .lzma, .zst).  Returns an InpImage; check .ok == 1.
 *     Caller must call inp_free() when done.
 *
 *   void inp_free(InpImage *img);
 *     Free the pixel buffer owned by an InpImage.
 */

#ifndef INP_H
#define INP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Unsigned 8-bit type used for pixel data.  Uses the same type as SDL2
 * so frontends can pass pixel buffers directly.  If your frontend doesn't
 * define Uint8, just typedef it to unsigned char. */
#ifndef Uint8
typedef unsigned char Uint8;
#endif

#ifndef Uint32
typedef unsigned int Uint32;
#endif

/* Color modes (stored in InpHeader.colors). */
enum {
    INP_MODE_16BIT,
    INP_MODE_256COLOR,
    INP_MODE_256GRAY,
    INP_MODE_16COLOR,
    INP_MODE_16GRAY,
    INP_MODE_BW
};

/* Parsed INP file header. */
typedef struct {
    int width;
    int height;
    int alpha;
    int colors;                 /* INP_MODE_* */
    Uint8 bg_r, bg_g, bg_b, bg_a;
    Uint8 palette[256][4];      /* RGBA per entry */
    int palette_count;
} InpHeader;

/* Result of loading a single INP image. */
typedef struct {
    InpHeader hdr;
    Uint8 *pixels;              /* RGBA, width*height*4 bytes, caller frees */
    int width, height;
    int ok;                     /* 1 on success, 0 on failure */
} InpImage;

/* Load an INP file and decode it into RGBA pixels.
 * Supports stdin ("-"), and auto-decompresses .xz/.gz/.bz2/.lzma/.zst. */
InpImage inp_load(const char *path);

/* Free the pixel buffer inside an InpImage. */
void inp_free(InpImage *img);

#ifdef __cplusplus
}
#endif

#endif /* INP_H */
