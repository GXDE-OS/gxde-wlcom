// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <pixman.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <kywc/log.h>
#include <wlr/backend/session.h>

#include "fbdev_p.h"

enum dpms_mode {
    DPMS_MODE_ON = 0,
    DPMS_MODE_OFF,
};

struct fbdev_state {
    const struct wlr_output_state *base;
    struct fb_var_screeninfo mode_info;
};

static const uint32_t COMMIT_OUTPUT_STATE =
    WLR_OUTPUT_STATE_BUFFER | WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED;

static const uint32_t SUPPORTED_OUTPUT_STATE =
    WLR_OUTPUT_STATE_BACKEND_OPTIONAL | COMMIT_OUTPUT_STATE;

struct fbdev_output *fbdev_output_from_output(struct wlr_output *wlr_output)
{
    assert(wlr_output_is_fbdev(wlr_output));
    struct fbdev_output *output = wl_container_of(wlr_output, output, wlr_output);
    return output;
}

static uint32_t calculate_drm_format(struct fb_var_screeninfo *vinfo,
                                     struct fb_fix_screeninfo *finfo)
{
    kywc_log(KYWC_DEBUG,
             "calculating drm format from: \n"
             "                        type: %i, aux: %i, visual: %i, bpp: %i, grayscale: %i\n"
             "                        red(offset: %i, length: %i, MSB: %i)\n"
             "                        green(offset: %i, length: %i, MSB: %i)\n"
             "                        blue(offset: %i, length: %i, MSB: %i)\n"
             "                        transp(offset: %i, length: %i, MSB: %i)",
             finfo->type, finfo->type_aux, finfo->visual, vinfo->bits_per_pixel, vinfo->grayscale,
             vinfo->red.offset, vinfo->red.length, vinfo->red.msb_right, vinfo->green.offset,
             vinfo->green.length, vinfo->green.msb_right, vinfo->blue.offset, vinfo->blue.length,
             vinfo->blue.msb_right, vinfo->transp.offset, vinfo->transp.length,
             vinfo->transp.msb_right);
    /* We only handle packed formats at the moment */
    if (finfo->type != FB_TYPE_PACKED_PIXELS) {
        return DRM_FORMAT_INVALID;
    }

    /* We only handle true-colour frame buffers at the moment */
    switch (finfo->visual) {
    case FB_VISUAL_TRUECOLOR:
    case FB_VISUAL_DIRECTCOLOR:
        if (vinfo->grayscale != 0) {
            return DRM_FORMAT_INVALID;
        }
        break;
    default:
        return DRM_FORMAT_INVALID;
    }

    /* We only support formats with MSBs on the left */
    if (vinfo->red.msb_right != 0 || vinfo->green.msb_right != 0 || vinfo->blue.msb_right != 0) {
        return DRM_FORMAT_INVALID;
    }

    /**
     * Work out the format type from the offsets. We only support RGBA,ARGB
     * and ABGR at the moment
     */
    if (vinfo->bits_per_pixel == 16) {
        return DRM_FORMAT_RGB565;
    } else if (vinfo->bits_per_pixel == 24) {
        return DRM_FORMAT_RGB888;
    } else if ((vinfo->transp.offset >= vinfo->red.offset || vinfo->transp.length == 0) &&
               vinfo->red.offset >= vinfo->green.offset &&
               vinfo->green.offset >= vinfo->blue.offset) {
        return DRM_FORMAT_ARGB8888;
    } else if (vinfo->red.offset >= vinfo->green.offset &&
               vinfo->green.offset >= vinfo->blue.offset &&
               vinfo->blue.offset >= vinfo->transp.offset) {
        return DRM_FORMAT_RGBA8888;
    } else if (vinfo->transp.offset >= vinfo->blue.offset &&
               vinfo->blue.offset >= vinfo->green.offset &&
               vinfo->green.offset >= vinfo->red.offset) {
        return DRM_FORMAT_ABGR8888;
    }

    return DRM_FORMAT_INVALID;
}

static bool fbdev_wakeup_screen(int fd, struct fbdev_screeninfo *info)
{
    struct fb_var_screeninfo varinfo;
    /* Grab the current screen information */
    if (ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
        return false;
    }

    /* force the framebuffer to wake up */
    varinfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    /* Set the device's screen information */
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &varinfo) < 0) {
        return false;
    }

    return true;
}

static uint32_t calculate_refresh_rate(struct fb_var_screeninfo *vinfo)
{
    uint64_t quot;
    /* Calculate monitor refresh rate. Default is 60 Hz. Units are mHz */
    quot = (vinfo->upper_margin + vinfo->lower_margin + vinfo->yres);
    quot *= (vinfo->left_margin + vinfo->right_margin + vinfo->xres);
    quot *= vinfo->pixclock;
    if (quot > 0) {
        uint64_t refresh_rate;
        refresh_rate = 1000000000000000LLU / quot;
        if (refresh_rate > 200000) {
            refresh_rate = 200000; /* cap at 200 Hz */
        }
        if (refresh_rate >= 1000) { /* at least 1 Hz */
            return refresh_rate;
        }
    }

    return 60 * 1000; /* default to 60 Hz */
}

static bool fbdev_query_screen_info(int fd, struct fbdev_screeninfo *info)
{
    struct fb_var_screeninfo varinfo;
    struct fb_fix_screeninfo fixinfo;
    /* Probe the device for screen information */
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fixinfo) < 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
        return false;
    }

    /* Store the pertinent data */
    info->current = varinfo;
    info->x_resolution = varinfo.xres;
    info->y_resolution = varinfo.yres;
    info->width_mm = varinfo.width == 0xFFFFFFFF ? 0 : varinfo.width;
    info->height_mm = varinfo.height == 0xFFFFFFFF ? 0 : varinfo.height;

    info->bits_per_pixel = varinfo.bits_per_pixel;
    info->buffer_length = fixinfo.smem_len;
    info->line_length = fixinfo.line_length;
    strncpy(info->desc, fixinfo.id, sizeof(info->desc));

    info->refresh_rate = calculate_refresh_rate(&varinfo);
    info->pixel_format = calculate_drm_format(&varinfo, &fixinfo);
    if (info->pixel_format == DRM_FORMAT_INVALID) {
        kywc_log(KYWC_WARN, "Frame buffer uses an unsupported format");
        return false;
    }

    return true;
}

static void fbdev_dpms_set(struct fbdev_output *output, enum dpms_mode mode)
{
    if (!output->dpms_mode_support) {
        return;
    }

    kywc_log(KYWC_DEBUG, "fbdev output dpms set");
    unsigned long fbmode = mode == DPMS_MODE_ON ? 0 : 4;

RETRY:
    if (ioctl(output->fd, FBIOBLANK, (void *)fbmode) == -1) {
        kywc_log_errno(KYWC_ERROR, "FBIOBLANK");
        if (errno == EAGAIN) {
            return;
        }
        if (errno == ERESTART || errno == EINTR) {
            goto RETRY;
        }
        output->dpms_mode_support = false;
        return;
    }

    output->dpms_mode_support = true;
}

static int fbdev_frame_buffer_open(const char *fb_dev, struct fbdev_screeninfo *screen_info)
{
    int fd = -1;
    fd = open(fb_dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to open frame buffer device %s", fb_dev);
        return -1;
    }

    /* Grab the screen info */
    if (!fbdev_query_screen_info(fd, screen_info)) {
        kywc_log_errno(KYWC_ERROR, "Failed to get frame buffer info");
        close(fd);
        return -1;
    }

    if (!fbdev_wakeup_screen(fd, screen_info)) {
        kywc_log(KYWC_ERROR, "Failed to activate framebuffer display. "
                             "Attempting to open output anyway");
    }
    return fd;
}

/* Closes the FD on success or failure */
static bool fbdev_frame_buffer_map(struct fbdev_output *output)
{
    kywc_log(KYWC_INFO, "mapping fbdev frame buffer");
    /* Map the frame buffer. Write mode */
    output->fb =
        mmap(NULL, output->screen_info.buffer_length, PROT_WRITE, MAP_SHARED, output->fd, 0);

    if (output->fb == MAP_FAILED) {
        kywc_log_errno(KYWC_ERROR, "Failed to mmap frame buffer");
        output->fb = NULL;
        return false;
    }

    return true;
}

static void fbdev_frame_buffer_unmap(struct fbdev_output *output)
{
    if (!output->fb) {
        return;
    }

    kywc_log(KYWC_INFO, "Unmapping fbdev frame buffer");
    if (munmap(output->fb, output->screen_info.buffer_length) < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to munmap frame buffer");
    }
    output->fb = NULL;
}

static bool fbdev_set_screen_info(int fd, struct fb_var_screeninfo *varinfo, bool test_only)
{
    varinfo->activate = test_only ? FB_ACTIVATE_TEST : FB_ACTIVATE_FORCE;
    /* Set the device's screen information */
    if (ioctl(fd, FBIOPUT_VSCREENINFO, varinfo) < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to FBIOPUT VSCREENINFO");
        return false;
    }

    return true;
}

static bool fbdev_fix_screen_info(int fd, struct fbdev_screeninfo *info)
{
    struct fb_var_screeninfo varinfo;
    /* Grab the current screen information */
    if (ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to FBIOGET VSCREENINFO");
        return false;
    }

    /* Update the information. */
    varinfo.xres = info->x_resolution;
    varinfo.yres = info->y_resolution;
    varinfo.width = info->width_mm;
    varinfo.height = info->height_mm;
    varinfo.bits_per_pixel = info->bits_per_pixel;

    /* Try to set up an ARGB (x8r8g8b8) pixel format */
    varinfo.grayscale = 0;
    varinfo.transp.offset = 24;
    varinfo.transp.length = 0;
    varinfo.transp.msb_right = 0;
    varinfo.red.offset = 16;
    varinfo.red.length = 8;
    varinfo.red.msb_right = 0;
    varinfo.green.offset = 8;
    varinfo.green.length = 8;
    varinfo.green.msb_right = 0;
    varinfo.blue.offset = 0;
    varinfo.blue.length = 8;
    varinfo.blue.msb_right = 0;

    return fbdev_set_screen_info(fd, &varinfo, false);
}

static void output_destroy(struct wlr_output *wlr_output)
{
    struct fbdev_output *output = fbdev_output_from_output(wlr_output);
    wl_list_remove(&output->link);
    if (output->frame_timer) {
        wl_event_source_remove(output->frame_timer);
    }
    free((void *)output->device);

    free(output);
}

bool fbdev_output_offscreen(struct fbdev_output *output)
{
    /* Unmap frame_buffer */
    fbdev_frame_buffer_unmap(output);

    if (output->backend->session->active) {
        return false;
    }

    wl_event_source_remove(output->frame_timer);
    output->frame_timer = NULL;

    close(output->fd);

    return true;
}

static bool fbdev_output_disable(struct fbdev_output *output)
{
    if (!output->wlr_output.enabled) {
        return true;
    }

    /* dpms off output */
    fbdev_dpms_set(output, DPMS_MODE_OFF);
    /* Unmap frame_buffer*/
    fbdev_frame_buffer_unmap(output);

    wl_event_source_remove(output->frame_timer);
    output->frame_timer = NULL;
    output->wlr_output.enabled = false;

    kywc_log(KYWC_INFO, "fbdev output disbled");

    return true;
}

static int signal_frame_handler(void *data)
{
    struct fbdev_output *output = data;
    wlr_output_send_frame(&output->wlr_output);
    return 0;
}

static bool fbdev_output_enable(struct fbdev_output *output)
{
    /* dpms on output */
    fbdev_dpms_set(output, DPMS_MODE_ON);
    /* Map frame_buffer*/
    fbdev_frame_buffer_map(output);

    struct wl_event_loop *ev = wl_display_get_event_loop(output->backend->display);
    output->frame_timer = wl_event_loop_add_timer(ev, signal_frame_handler, output);

    kywc_log(KYWC_INFO, "fbdev output enabled");
    output->wlr_output.enabled = true;

    return true;
}

struct deferred_present_event {
    struct wlr_output *output;
    struct wl_event_source *idle_source;
    struct wlr_output_event_present event;
    struct wl_listener output_destroy;
};

static bool output_pending_enabled(struct wlr_output *output, const struct wlr_output_state *state)
{
    if (state->committed & WLR_OUTPUT_STATE_ENABLED) {
        return state->enabled;
    }

    return output->enabled;
}

static void deferred_present_event_destroy(struct deferred_present_event *deferred)
{
    wl_list_remove(&deferred->output_destroy.link);
    free(deferred);
}

static void deferred_present_event_handle_idle(void *data)
{
    struct deferred_present_event *deferred = data;
    wlr_output_send_present(deferred->output, &deferred->event);
    deferred_present_event_destroy(deferred);
}

static void deferred_present_event_handle_output_destroy(struct wl_listener *listener, void *data)
{
    struct deferred_present_event *deferred = wl_container_of(listener, deferred, output_destroy);
    wl_event_source_remove(deferred->idle_source);
    deferred_present_event_destroy(deferred);
}

static void output_defer_present(struct wlr_output *output, struct wlr_output_event_present event)
{
    struct deferred_present_event *deferred = calloc(1, sizeof(*deferred));
    if (!deferred) {
        return;
    }
    *deferred = (struct deferred_present_event){
        .output = output,
        .event = event,
    };
    deferred->output_destroy.notify = deferred_present_event_handle_output_destroy;
    wl_signal_add(&output->events.destroy, &deferred->output_destroy);

    struct wl_event_loop *ev = wl_display_get_event_loop(output->display);
    deferred->idle_source =
        wl_event_loop_add_idle(ev, deferred_present_event_handle_idle, deferred);
}

static bool fbdev_output_state_update_fb(struct fbdev_output *output,
                                         const struct wlr_output_state *state)
{
    void *data;
    uint32_t format;
    size_t stride;
    if (!wlr_buffer_begin_data_ptr_access(state->buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data,
                                          &format, &stride)) {
        return false;
    }

    assert(output->screen_info.x_resolution * output->screen_info.bits_per_pixel / 8 == stride);

    pixman_region32_t clipped;
    pixman_region32_init(&clipped);
    pixman_region32_init_rect(&clipped, 0, 0, output->screen_info.x_resolution,
                              output->screen_info.y_resolution);

    if (state->committed & WLR_OUTPUT_STATE_DAMAGE) {
        pixman_region32_intersect_rect(&clipped, &state->damage, 0, 0, state->buffer->width,
                                       state->buffer->height);
    }

    /* Copy shadow fb */
    uint32_t padding = output->screen_info.line_length - stride;
    uint32_t pixel_bytes = output->screen_info.bits_per_pixel / 8;
    uint32_t offset, size, offset_src, offset_dst;
    uint8_t *src, *dst;

    int rects_len;
    const pixman_box32_t *rects = pixman_region32_rectangles(&clipped, &rects_len);
    for (int i = 0; i < rects_len; ++i) {
        if (rects[i].x2 - rects[i].x1 == state->buffer->width && padding == 0) {
            offset = stride * rects[i].y1;
            src = (uint8_t *)data + offset;
            dst = (uint8_t *)output->fb + offset;
            size = (rects[i].y2 - rects[i].y1) * stride;
            memcpy(dst, src, size);
            continue;
        }

        for (int32_t y = rects[i].y1; y < rects[i].y2; ++y) {
            offset_src = y * stride + rects[i].x1 * pixel_bytes;
            src = (uint8_t *)data + offset_src;
            offset_dst = y * (stride + padding) + rects[i].x1 * pixel_bytes;
            dst = (uint8_t *)output->fb + offset_dst;
            size = (rects[i].x2 - rects[i].x1) * pixel_bytes;
            memcpy(dst, src, size);
        }
    }

    pixman_region32_fini(&clipped);
    wlr_buffer_end_data_ptr_access(state->buffer);

    return true;
}

static void fbdev_output_update_refresh(struct fbdev_output *output, int32_t refresh)
{
    if (refresh <= 0) {
        refresh = 60 * 1000; // 60 HZ
    }
    output->frame_delay = 1000000 / refresh;
}

static bool fbdev_modes_equal(struct fb_var_screeninfo *set, struct fb_var_screeninfo *req)
{
    return (set->xres_virtual >= req->xres_virtual && set->yres_virtual >= req->yres_virtual &&
            set->bits_per_pixel == req->bits_per_pixel && set->red.length == req->red.length &&
            set->green.length == req->green.length && set->blue.length == req->blue.length &&
            set->xres == req->xres && set->yres == req->yres &&
            set->right_margin == req->right_margin && set->hsync_len == req->hsync_len &&
            set->left_margin == req->left_margin && set->lower_margin == req->lower_margin &&
            set->vsync_len == req->vsync_len && set->upper_margin == req->upper_margin &&
            set->sync == req->sync && set->vmode == req->vmode);
}

static bool fbdev_output_test(struct wlr_output *wlr_output, struct fbdev_state *state)
{
    struct fbdev_output *output = fbdev_output_from_output(wlr_output);
    if (!output->backend->session->active) {
        return false;
    }

    uint32_t unsupported = state->base->committed & ~SUPPORTED_OUTPUT_STATE;
    if (unsupported != 0) {
        kywc_log(KYWC_DEBUG, "unsupported output state fields: 0x%" PRIx32, unsupported);
        return false;
    }

    if ((state->base->committed & COMMIT_OUTPUT_STATE) == 0) {
        // This commit doesn't change the fbdev state
        return true;
    };

    if (state->base->committed & WLR_OUTPUT_STATE_MODE) {
        struct fbdev_mode *mode = wl_container_of(state->base->mode, mode, wlr_mode);
        state->mode_info = mode->mode_info;
        return state->base->mode_type != WLR_OUTPUT_STATE_MODE_CUSTOM;
    }

    return true;
}

static bool fbdev_output_commit(struct wlr_output *wlr_output, const struct wlr_output_state *state)
{
    struct fbdev_output *output = fbdev_output_from_output(wlr_output);
    if (!output->backend->session->active) {
        return false;
    }

    struct fbdev_state pending = { .base = state };
    if (!fbdev_output_test(wlr_output, &pending)) {
        kywc_log(KYWC_ERROR, "fbdev output test faield");
        return false;
    }

    if (pending.base->committed & WLR_OUTPUT_STATE_MODE) {
        if (!fbdev_modes_equal(&pending.mode_info, &output->screen_info.current) &&
            !fbdev_set_screen_info(output->fd, &pending.mode_info, false)) {
            return false;
        }

        fbdev_query_screen_info(output->fd, &output->screen_info);
        fbdev_output_update_refresh(output, pending.base->mode->refresh);
        wlr_output->current_mode = pending.base->mode;
    }

    if (pending.base->committed & WLR_OUTPUT_STATE_ENABLED) {
        if (pending.base->enabled && !wlr_output->enabled) {
            fbdev_output_enable(output);
        } else if (!pending.base->enabled && wlr_output->enabled) {
            fbdev_output_disable(output);
        }
    }

    if (pending.base->committed & WLR_OUTPUT_STATE_BUFFER && output->wlr_output.enabled) {
        fbdev_output_state_update_fb(output, pending.base);
    }

    if (output_pending_enabled(wlr_output, pending.base)) {
        struct wlr_output_event_present present_event = {
            .commit_seq = wlr_output->commit_seq + 1,
            .presented = true,
        };
        output_defer_present(wlr_output, present_event);
        wl_event_source_timer_update(output->frame_timer, output->frame_delay);
    }

    return true;
}

static const struct wlr_output_impl output_impl = {
    .destroy = output_destroy,
    .commit = fbdev_output_commit,
};

struct wlr_output *fbdev_output_create(struct wlr_backend *wlr_backend, const char *device)
{
    struct fbdev_output *output = calloc(1, sizeof(*output));
    /* Create the frame buffer */
    int fd = fbdev_frame_buffer_open(device, &output->screen_info);
    if (fd < 0) {
        kywc_log(KYWC_ERROR, "Creating frame buffer output failed");
        free(output);
        return NULL;
    }

    output->fd = fd;
    output->dpms_mode_support = true;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    struct fbdev_backend *backend = get_fbdev_backend_from_backend(wlr_backend);
    wlr_output_init(&output->wlr_output, wlr_backend, &output_impl, backend->display, &state);
    wlr_output_state_finish(&state);

    wlr_output_set_name(&output->wlr_output, "FB-1");
    wlr_output_set_description(&output->wlr_output, output->screen_info.desc);
    kywc_log(KYWC_INFO, "fbdev desc: %s", output->screen_info.desc);

    wlr_output_set_subpixel(&output->wlr_output, WL_OUTPUT_SUBPIXEL_UNKNOWN);
    wlr_output_set_render_format(&output->wlr_output, output->screen_info.pixel_format);
    kywc_log(KYWC_INFO, "fbdev output render format: 0x%" PRIX32, output->screen_info.pixel_format);

    wlr_output_lock_software_cursors(&output->wlr_output, true);
    output->wlr_output.phys_width = output->screen_info.width_mm;
    output->wlr_output.phys_height = output->screen_info.height_mm;
    kywc_log(KYWC_DEBUG, "screen physic_size: %" PRId32 "x%" PRId32 " (mm)",
             output->screen_info.width_mm, output->screen_info.height_mm);

    /* only one static builtin mode in list */
    struct fbdev_mode *mode = &output->mode;
    mode->wlr_mode.width = output->screen_info.x_resolution;
    mode->wlr_mode.height = output->screen_info.y_resolution;
    mode->wlr_mode.refresh = output->screen_info.refresh_rate;
    mode->mode_info = output->screen_info.current;
    wl_list_insert(&output->wlr_output.modes, &mode->wlr_mode.link);

    kywc_log(KYWC_INFO, "Detected modes:");
    kywc_log(KYWC_INFO, "  %" PRId32 "x%" PRId32 " @ %.3f Hz", mode->wlr_mode.width,
             mode->wlr_mode.height, (float)mode->wlr_mode.refresh / 1000);

    output->device = strdup(device);
    output->backend = backend;

    wl_list_insert(&backend->outputs, &output->link);

    return &output->wlr_output;
}

bool wlr_output_is_fbdev(struct wlr_output *wlr_output)
{
    return wlr_output->impl == &output_impl;
}

bool fbdev_output_reenable(struct fbdev_output *output)
{
    kywc_log(KYWC_INFO, "re-enabling fbdev output");

    if (!output->wlr_output.enabled) {
        return false;
    }

    /* Create the frame buffer */
    struct fbdev_screeninfo new_screen_info;
    int fd = fbdev_frame_buffer_open(output->device, &new_screen_info);
    if (fd < 0) {
        kywc_log(KYWC_ERROR, "Creating frame buffer failed");
        return false;
    }

    output->fd = fd;
    output->dpms_mode_support = true;

    /* Check whether the frame buffer details have changed since we were disable */
    if (output->screen_info.x_resolution != new_screen_info.x_resolution ||
        output->screen_info.y_resolution != new_screen_info.y_resolution ||
        output->screen_info.width_mm != new_screen_info.width_mm ||
        output->screen_info.height_mm != new_screen_info.height_mm ||
        output->screen_info.bits_per_pixel != new_screen_info.bits_per_pixel ||
        output->screen_info.pixel_format != new_screen_info.pixel_format ||
        output->screen_info.refresh_rate != new_screen_info.refresh_rate) {

        /* Perform a mode-set to restore the old mode */
        if (!fbdev_fix_screen_info(output->fd, &output->screen_info)) {
            kywc_log(KYWC_ERROR, "Failed to restore mode settings. "
                                 "Attempting to re-open output anyway");
        }
    }

    fbdev_output_enable(output);

    return true;
}
