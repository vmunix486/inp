/* inpview-x11.c -- X11/Xlib frontend for INP image viewer
 * Usage: inpview-x11 [--slideshow=N] <files...>
 *
 * Written in strict C89: all declarations sit at the top of their block.
 */

#include "inp.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Title bar                                                           */
/* ------------------------------------------------------------------ */

static void update_title(Display *dpy, Window win, const char *path, int idx, int total)
{
    char title[512];
    if (total > 1)
        snprintf(title, sizeof(title), "inpview-x11: %s (%d/%d)", path, idx + 1, total);
    else
        snprintf(title, sizeof(title), "inpview-x11: %s", path);
    XStoreName(dpy, win, title);
    XFlush(dpy);
}

/* ------------------------------------------------------------------ */
/* Image switching                                                     */
/* ------------------------------------------------------------------ */

static int switch_image(const char *path, InpImage *img)
{
    InpImage next;
    next = inp_load(path);
    if (!next.ok) return 0;
    inp_free(img);
    *img = next;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Pixel conversion: RGBA -> X11 TrueColor format                      */
/* ------------------------------------------------------------------ */

static unsigned char *convert_pixels(const unsigned char *src,
                                      int w, int h,
                                      unsigned long rmask, unsigned long gmask,
                                      unsigned long bmask, int bpp)
{
    unsigned char *dst;
    int i;
    int rshift, gshift, bshift;
    unsigned long m;

    rshift = 0; m = rmask; while ((m & 1) == 0) { rshift++; m >>= 1; }
    gshift = 0; m = gmask; while ((m & 1) == 0) { gshift++; m >>= 1; }
    bshift = 0; m = bmask; while ((m & 1) == 0) { bshift++; m >>= 1; }

    if (bpp == 4) {
        dst = (unsigned char *)malloc((size_t)w * h * 4);
        if (!dst) return NULL;
        for (i = 0; i < w * h; i++) {
            unsigned int pixel = 0;
            pixel |= (unsigned int)src[i * 4 + 0] << rshift;
            pixel |= (unsigned int)src[i * 4 + 1] << gshift;
            pixel |= (unsigned int)src[i * 4 + 2] << bshift;
            memcpy(dst + i * 4, &pixel, 4);
        }
        return dst;
    }

    if (bpp == 3) {
        dst = (unsigned char *)malloc((size_t)w * h * 3);
        if (!dst) return NULL;
        for (i = 0; i < w * h; i++) {
            unsigned int pixel = 0;
            pixel |= (unsigned int)src[i * 4 + 0] << rshift;
            pixel |= (unsigned int)src[i * 4 + 1] << gshift;
            pixel |= (unsigned int)src[i * 4 + 2] << bshift;
            dst[i * 3 + 0] = (unsigned char)(pixel >> 16);
            dst[i * 3 + 1] = (unsigned char)(pixel >> 8);
            dst[i * 3 + 2] = (unsigned char)(pixel);
        }
        return dst;
    }

    dst = (unsigned char *)malloc((size_t)w * h * 4);
    if (!dst) return NULL;
    memcpy(dst, src, (size_t)w * h * 4);
    return dst;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    Display *dpy;
    int screen;
    Window root, win;
    XVisualInfo vinfo;
    Colormap cmap;
    GC gc;
    XEvent ev;
    Atom wm_delete, net_wm_state, net_wm_state_fullscreen;

    int file_count, slideshow_ms, cur;
    const char *files[4096];

    InpImage img;
    unsigned char *xbuf;
    int xbuf_bpp;
    XImage *ximg;
    int rshift, gshift, bshift;

    unsigned int win_w, win_h;
    Pixmap back_pixmap;
    float zoom, pan_x, pan_y;
    int dragging, drag_x, drag_y;
    Uint32 last_advance;
    int running, needs_redraw;
    int i;

    /* parse args */
    file_count = 0;
    slideshow_ms = 0;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--slideshow=", 12) == 0) {
            slideshow_ms = atoi(argv[i] + 12) * 1000;
            if (slideshow_ms < 0) slideshow_ms = 0;
        } else {
            if (file_count >= 4096) {
                fprintf(stderr, "inpview-x11: too many files\n");
                return 1;
            }
            files[file_count++] = argv[i];
        }
    }
    if (file_count == 0) {
        fprintf(stderr, "usage: %s [--slideshow=N] <files...>\n", argv[0]);
        return 1;
    }

    /* load first image */
    img = inp_load(files[0]);
    if (!img.ok) return 1;

    /* open X display */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "inpview-x11: cannot open X display\n");
        inp_free(&img);
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    /* find a TrueColor visual with depth >= 24 */
    if (!XMatchVisualInfo(dpy, screen, 24, TrueColor, &vinfo)) {
        if (!XMatchVisualInfo(dpy, screen, 32, TrueColor, &vinfo)) {
            fprintf(stderr, "inpview-x11: no 24/32-bit TrueColor visual\n");
            XCloseDisplay(dpy);
            inp_free(&img);
            return 1;
        }
    }

    cmap = XCreateColormap(dpy, root, vinfo.visual, AllocNone);

    /* create window */
    {
        XSetWindowAttributes attr;
        attr.colormap = cmap;
        attr.border_pixel = 0;
        attr.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                          ButtonReleaseMask | PointerMotionMask |
                          StructureNotifyMask;
        win_w = (unsigned int)img.width;
        win_h = (unsigned int)img.height;
        win = XCreateWindow(dpy, root, 0, 0, win_w, win_h, 0,
                            vinfo.depth, InputOutput, vinfo.visual,
                            CWColormap | CWBorderPixel | CWEventMask, &attr);
    }

    gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, 0);
    XSetBackground(dpy, gc, 0);

    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

    XMapWindow(dpy, win);
    XFlush(dpy);

    /* create backing pixmap for flicker-free rendering */
    back_pixmap = XCreatePixmap(dpy, win, win_w, win_h, vinfo.depth);

    /* convert initial image pixels to X11 format */
    xbuf_bpp = 4;
    xbuf = convert_pixels(img.pixels, img.width, img.height,
                          vinfo.red_mask, vinfo.green_mask, vinfo.blue_mask,
                          xbuf_bpp);
    if (!xbuf) {
        fprintf(stderr, "inpview-x11: out of memory\n");
        inp_free(&img);
        XFreeGC(dpy, gc);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    ximg = XCreateImage(dpy, vinfo.visual, vinfo.depth, ZPixmap, 0,
                        (char *)xbuf, img.width, img.height, xbuf_bpp * 8,
                        img.width * xbuf_bpp);
    if (!ximg) {
        fprintf(stderr, "inpview-x11: XCreateImage failed\n");
        free(xbuf);
        inp_free(&img);
        XFreeGC(dpy, gc);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    update_title(dpy, win, files[0], 0, file_count);

    zoom = 1.0f;
    pan_x = 0.0f;
    pan_y = 0.0f;
    dragging = 0;
    drag_x = 0;
    drag_y = 0;
    cur = 0;
    last_advance = 0;
    running = 1;
    needs_redraw = 1;

    /* compute pixel format shifts once */
    {
        unsigned long m;
        rshift = 0; m = vinfo.red_mask;   while ((m & 1) == 0) { rshift++; m >>= 1; }
        gshift = 0; m = vinfo.green_mask; while ((m & 1) == 0) { gshift++; m >>= 1; }
        bshift = 0; m = vinfo.blue_mask;  while ((m & 1) == 0) { bshift++; m >>= 1; }
    }

    while (running) {
        /* process all pending events */
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);

            if (ev.type == Expose && ev.xexpose.count == 0) {
                needs_redraw = 1;
            }

            if (ev.type == ConfigureNotify) {
                win_w = (unsigned int)ev.xconfigure.width;
                win_h = (unsigned int)ev.xconfigure.height;
                if (back_pixmap) XFreePixmap(dpy, back_pixmap);
                back_pixmap = XCreatePixmap(dpy, win, win_w, win_h, vinfo.depth);
                needs_redraw = 1;
            }

            if (ev.type == ClientMessage &&
                (Atom)ev.xclient.data.l[0] == wm_delete) {
                running = 0;
            }

            if (ev.type == KeyPress) {
                KeySym ks;
                char buf[8];
                int nbytes;
                int want;

                nbytes = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
                (void)nbytes;

                if (ks == XK_Escape || ks == XK_q) running = 0;

                if (ks == XK_F11) {
                    XEvent ce;
                    memset(&ce, 0, sizeof(ce));
                    ce.type = ClientMessage;
                    ce.xclient.window = win;
                    ce.xclient.message_type = net_wm_state;
                    ce.xclient.format = 32;
                    ce.xclient.data.l[0] = 2;
                    ce.xclient.data.l[1] = net_wm_state_fullscreen;
                    ce.xclient.data.l[2] = 0;
                    ce.xclient.send_event = True;
                    XSendEvent(dpy, root, False,
                               SubstructureRedirectMask | SubstructureNotifyMask, &ce);
                }

                if (ks == XK_0) {
                    zoom = 1.0f;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
                    needs_redraw = 1;
                }

                /* gallery navigation */
                want = -1;
                if (file_count > 1) {
                    if (ks == XK_Right || ks == XK_space)
                        want = (cur + 1) % file_count;
                    else if (ks == XK_Left)
                        want = (cur - 1 + file_count) % file_count;
                    else if (ks == XK_Home)
                        want = 0;
                    else if (ks == XK_End)
                        want = file_count - 1;
                }

                if (want >= 0 && want != cur) {
                    if (switch_image(files[want], &img)) {
                        XDestroyImage(ximg);
                        xbuf = convert_pixels(img.pixels, img.width, img.height,
                                              vinfo.red_mask, vinfo.green_mask,
                                              vinfo.blue_mask, xbuf_bpp);
                        if (!xbuf) { running = 0; break; }

                        ximg = XCreateImage(dpy, vinfo.visual, vinfo.depth, ZPixmap, 0,
                                            (char *)xbuf, img.width, img.height,
                                            xbuf_bpp * 8, img.width * xbuf_bpp);
                        if (!ximg) { running = 0; break; }

                        cur = want;
                        zoom = 1.0f;
                        pan_x = 0.0f;
                        pan_y = 0.0f;
                        update_title(dpy, win, files[cur], cur, file_count);
                        needs_redraw = 1;
                    }
                }

                /* zoom */
                if (ks == XK_equal || ks == XK_plus) {
                    zoom *= 1.25f;
                    if (zoom > 50.0f) zoom = 50.0f;
                    needs_redraw = 1;
                }
                if (ks == XK_minus) {
                    zoom /= 1.25f;
                    if (zoom < 0.05f) zoom = 0.05f;
                    needs_redraw = 1;
                }
            }

            if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button1) {
                    dragging = 1;
                    drag_x = ev.xbutton.x;
                    drag_y = ev.xbutton.y;
                }
                if (ev.xbutton.button == Button4) {
                    zoom *= 1.1f;
                    if (zoom > 50.0f) zoom = 50.0f;
                    needs_redraw = 1;
                }
                if (ev.xbutton.button == Button5) {
                    zoom /= 1.1f;
                    if (zoom < 0.05f) zoom = 0.05f;
                    needs_redraw = 1;
                }
            }
            if (ev.type == ButtonRelease && ev.xbutton.button == Button1) {
                dragging = 0;
            }
            if (ev.type == MotionNotify && dragging) {
                pan_x += (float)(ev.xmotion.x - drag_x);
                pan_y += (float)(ev.xmotion.y - drag_y);
                drag_x = ev.xmotion.x;
                drag_y = ev.xmotion.y;
                needs_redraw = 1;
            }
        }

        /* redraw once per frame if needed */
        if (needs_redraw) {
            int dst_w, dst_h, dst_x, dst_y;
            int base_w, base_h;

            needs_redraw = 0;

            if (img.width > 0 && img.height > 0 && win_w > 0 && win_h > 0) {
                if ((float)img.width / img.height > (float)win_w / win_h) {
                    base_w = (int)win_w;
                    base_h = (int)((float)win_w * img.height / img.width);
                } else {
                    base_h = (int)win_h;
                    base_w = (int)((float)win_h * img.width / img.height);
                }

                dst_w = (int)((float)base_w * zoom);
                dst_h = (int)((float)base_h * zoom);
                dst_x = ((int)win_w - dst_w) / 2 + (int)pan_x;
                dst_y = ((int)win_h - dst_h) / 2 + (int)pan_y;

                /* clear backing pixmap to black */
                XSetForeground(dpy, gc, 0);
                XFillRectangle(dpy, back_pixmap, gc, 0, 0, win_w, win_h);

                if (dst_w > 0 && dst_h > 0) {
                    int dx, dy, sx, sy;
                    int sw = img.width;
                    int sh = img.height;
                    int bpp = xbuf_bpp;
                    unsigned char *rowbuf;
                    XImage *tmp;

                    rowbuf = (unsigned char *)malloc((size_t)dst_w * bpp);
                    if (rowbuf) {
                        tmp = XCreateImage(dpy, vinfo.visual, vinfo.depth, ZPixmap, 0,
                                           (char *)rowbuf, dst_w, 1, bpp * 8, dst_w * bpp);
                        if (tmp) {
                            for (dy = 0; dy < dst_h; dy++) {
                                sy = (int)((long)dy * sh / dst_h);
                                if (sy >= sh) sy = sh - 1;
                                for (dx = 0; dx < dst_w; dx++) {
                                    sx = (int)((long)dx * sw / dst_w);
                                    if (sx >= sw) sx = sw - 1;
                                    {
                                        int si = (sy * sw + sx) * 4;
                                        int di = dx * bpp;
                                        unsigned int pixel = 0;
                                        pixel |= (unsigned int)img.pixels[si + 0] << rshift;
                                        pixel |= (unsigned int)img.pixels[si + 1] << gshift;
                                        pixel |= (unsigned int)img.pixels[si + 2] << bshift;
                                        memcpy(rowbuf + di, &pixel, bpp);
                                    }
                                }
                                XPutImage(dpy, back_pixmap, gc, tmp, 0, 0,
                                          dst_x, dst_y + dy, dst_w, 1);
                            }
                            tmp->data = NULL;
                            XDestroyImage(tmp);
                        }
                        free(rowbuf);
                    }
                }

                /* blit backing pixmap to window in one shot */
                XCopyArea(dpy, back_pixmap, win, gc, 0, 0, win_w, win_h, 0, 0);
            }
        }

        /* slideshow auto-advance */
        if (slideshow_ms > 0 && file_count > 1 && running) {
            struct timeval tv;
            Uint32 now;
            int want;

            gettimeofday(&tv, NULL);
            now = (Uint32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
            if (last_advance == 0) last_advance = now;

            if (now - last_advance >= (Uint32)slideshow_ms) {
                want = (cur + 1) % file_count;
                if (switch_image(files[want], &img)) {
                    XDestroyImage(ximg);
                    xbuf = convert_pixels(img.pixels, img.width, img.height,
                                          vinfo.red_mask, vinfo.green_mask,
                                          vinfo.blue_mask, xbuf_bpp);
                    if (!xbuf) { running = 0; break; }

                    ximg = XCreateImage(dpy, vinfo.visual, vinfo.depth, ZPixmap, 0,
                                        (char *)xbuf, img.width, img.height,
                                        xbuf_bpp * 8, img.width * xbuf_bpp);
                    if (!ximg) { running = 0; break; }

                    cur = want;
                    zoom = 1.0f;
                    pan_x = 0.0f;
                    pan_y = 0.0f;
                    last_advance = now;
                    update_title(dpy, win, files[cur], cur, file_count);
                    needs_redraw = 1;
                } else {
                    last_advance = now;
                }
            }
        }

        if (!needs_redraw)
            usleep(16000);
    }

    XDestroyImage(ximg);
    if (back_pixmap) XFreePixmap(dpy, back_pixmap);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XFreeColormap(dpy, cmap);
    XCloseDisplay(dpy);
    inp_free(&img);

    return 0;
}
