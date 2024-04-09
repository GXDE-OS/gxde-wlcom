// SPDX-FileCopyrightText: 2023 The wlroots contributors
// SPDX-FileCopyrightText: 2023 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: MulanPSL-2.0

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>

#include <kywc/log.h>

#include "render/egl.h"

#ifndef EGL_DRIVER_NAME_EXT
#define EGL_DRIVER_NAME_EXT 0x335E
#endif
#ifndef EGL_DRM_RENDER_NODE_FILE_EXT
#define EGL_DRM_RENDER_NODE_FILE_EXT 0x3377
#endif

static enum kywc_log_level egl_log_level_to_kywc(EGLint type)
{
    switch (type) {
    case EGL_DEBUG_MSG_CRITICAL_KHR:
        return KYWC_FATAL;
    case EGL_DEBUG_MSG_ERROR_KHR:
        return KYWC_ERROR;
    case EGL_DEBUG_MSG_WARN_KHR:
        return KYWC_WARN;
    case EGL_DEBUG_MSG_INFO_KHR:
        return KYWC_INFO;
    default:
        return KYWC_INFO;
    }
}

static const char *egl_error_str(EGLint error)
{
    switch (error) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_DEVICE_EXT:
        return "EGL_BAD_DEVICE_EXT";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
    }
    return "unknown error";
}

static void egl_log(EGLenum error, const char *command, EGLint msg_type, EGLLabelKHR thread,
                    EGLLabelKHR obj, const char *msg)
{
    kywc_log(egl_log_level_to_kywc(msg_type),
             "[EGL] command: %s, error: %s (0x%x), message: \"%s\"", command, egl_error_str(error),
             error, msg);
}

static void log_modifier(uint64_t modifier, bool external_only)
{
    char *mod_name = drmGetFormatModifierName(modifier);
    kywc_log(KYWC_DEBUG, "    %s (0x%016" PRIX64 "): ✓ texture  %s render",
             mod_name ? mod_name : "<unknown>", modifier, external_only ? "✗" : "✓");
    free(mod_name);
}

static int get_egl_dmabuf_formats(struct ky_egl *egl, EGLint **formats)
{
    if (!egl->exts.EXT_image_dma_buf_import) {
        kywc_log(KYWC_DEBUG, "DMA-BUF import extension not present");
        return -1;
    }

    // when we only have the image_dmabuf_import extension we can't query
    // which formats are supported. These two are on almost always
    // supported; it's the intended way to just try to create buffers.
    // Just a guess but better than not supporting dmabufs at all,
    // given that the modifiers extension isn't supported everywhere.
    if (!egl->exts.EXT_image_dma_buf_import_modifiers) {
        static const EGLint fallback_formats[] = {
            DRM_FORMAT_ARGB8888,
            DRM_FORMAT_XRGB8888,
        };
        int num = sizeof(fallback_formats) / sizeof(fallback_formats[0]);

        *formats = calloc(num, sizeof(**formats));
        if (!*formats) {
            kywc_log_errno(KYWC_ERROR, "Allocation failed");
            return -1;
        }

        memcpy(*formats, fallback_formats, num * sizeof(**formats));
        return num;
    }

    EGLint num;
    if (!eglQueryDmaBufFormatsEXT(egl->display, 0, NULL, &num)) {
        kywc_log(KYWC_ERROR, "Failed to query number of dmabuf formats");
        return -1;
    }

    *formats = calloc(num, sizeof(**formats));
    if (*formats == NULL) {
        kywc_log(KYWC_ERROR, "Allocation failed: %s", strerror(errno));
        return -1;
    }

    if (!eglQueryDmaBufFormatsEXT(egl->display, num, *formats, &num)) {
        kywc_log(KYWC_ERROR, "Failed to query dmabuf format");
        free(*formats);
        return -1;
    }
    return num;
}

static int get_egl_dmabuf_modifiers(struct ky_egl *egl, EGLint format, uint64_t **modifiers,
                                    EGLBoolean **external_only)
{
    *modifiers = NULL;
    *external_only = NULL;

    if (!egl->exts.EXT_image_dma_buf_import) {
        kywc_log(KYWC_DEBUG, "DMA-BUF extension not present");
        return -1;
    }
    if (!egl->exts.EXT_image_dma_buf_import_modifiers) {
        return 0;
    }

    EGLint num;
    if (!eglQueryDmaBufModifiersEXT(egl->display, format, 0, NULL, NULL, &num)) {
        kywc_log(KYWC_ERROR, "Failed to query dmabuf number of modifiers");
        return -1;
    }
    if (num == 0) {
        return 0;
    }

    *modifiers = calloc(num, sizeof(**modifiers));
    if (*modifiers == NULL) {
        kywc_log_errno(KYWC_ERROR, "Allocation failed");
        return -1;
    }
    *external_only = calloc(num, sizeof(**external_only));
    if (*external_only == NULL) {
        kywc_log_errno(KYWC_ERROR, "Allocation failed");
        free(*modifiers);
        *modifiers = NULL;
        return -1;
    }

    /* GL_TEXTURE_EXTERNAL_OES texture target when GL_OES_EGL_image_external */
    if (!eglQueryDmaBufModifiersEXT(egl->display, format, num, *modifiers, *external_only, &num)) {
        kywc_log(KYWC_ERROR, "Failed to query dmabuf modifiers");
        free(*modifiers);
        free(*external_only);
        return -1;
    }
    return num;
}

static void init_dmabuf_formats(struct ky_egl *egl)
{
    char *env = getenv("KYWC_EGL_NO_MODIFIERS");
    bool no_modifiers = env && strcmp(env, "1") == 0;
    if (no_modifiers) {
        kywc_log(KYWC_INFO, "KYWC_EGL_NO_MODIFIERS set, disabling modifiers for EGL");
    }

    EGLint *formats;
    int formats_len = get_egl_dmabuf_formats(egl, &formats);
    if (formats_len < 0) {
        return;
    }

    kywc_log(KYWC_DEBUG, "Supported DMA-BUF formats:");

    bool has_modifiers = false;
    for (int i = 0; i < formats_len; i++) {
        EGLint fmt = formats[i];

        uint64_t *modifiers = NULL;
        EGLBoolean *external_only = NULL;
        int modifiers_len = 0;
        if (!no_modifiers) {
            modifiers_len = get_egl_dmabuf_modifiers(egl, fmt, &modifiers, &external_only);
        }
        if (modifiers_len < 0) {
            continue;
        }

        has_modifiers = has_modifiers || modifiers_len > 0;

        bool all_external_only = true;
        for (int j = 0; j < modifiers_len; j++) {
            wlr_drm_format_set_add(&egl->dmabuf_texture_formats, fmt, modifiers[j]);
            if (!external_only[j]) {
                wlr_drm_format_set_add(&egl->dmabuf_render_formats, fmt, modifiers[j]);
                all_external_only = false;
            }
            int plane_count =
                gbm_device_get_format_modifier_plane_count(egl->gbm_device, fmt, modifiers[j]);
            if (plane_count == 1) {
                wlr_drm_format_set_add(&egl->dmabuf_render_single_plane_formats, fmt, modifiers[j]);
            }
        }

        // EGL always supports implicit modifiers. If at least one modifier supports rendering,
        // assume the implicit modifier supports rendering too.
        wlr_drm_format_set_add(&egl->dmabuf_texture_formats, fmt, DRM_FORMAT_MOD_INVALID);
        if (modifiers_len == 0 || !all_external_only) {
            wlr_drm_format_set_add(&egl->dmabuf_render_formats, fmt, DRM_FORMAT_MOD_INVALID);
            wlr_drm_format_set_add(&egl->dmabuf_render_single_plane_formats, fmt,
                                   DRM_FORMAT_MOD_INVALID);
        }

        if (modifiers_len == 0) {
            // Asume the linear layout is supported if the driver doesn't
            // explicitly say otherwise
            wlr_drm_format_set_add(&egl->dmabuf_texture_formats, fmt, DRM_FORMAT_MOD_LINEAR);
            wlr_drm_format_set_add(&egl->dmabuf_render_formats, fmt, DRM_FORMAT_MOD_LINEAR);
            wlr_drm_format_set_add(&egl->dmabuf_render_single_plane_formats, fmt,
                                   DRM_FORMAT_MOD_LINEAR);
        }

        if (kywc_log_get_level() >= KYWC_DEBUG) {
            char *fmt_name = drmGetFormatName(fmt);
            kywc_log(KYWC_DEBUG, "  %s (0x%08" PRIX32 ")", fmt_name ? fmt_name : "<unknown>", fmt);
            free(fmt_name);

            log_modifier(DRM_FORMAT_MOD_INVALID, false);
            if (modifiers_len == 0) {
                log_modifier(DRM_FORMAT_MOD_LINEAR, false);
            }
            for (int j = 0; j < modifiers_len; j++) {
                log_modifier(modifiers[j], external_only[j]);
            }
        }

        free(modifiers);
        free(external_only);
    }
    free(formats);

    egl->has_modifiers = has_modifiers;
    if (!no_modifiers) {
        kywc_log(KYWC_DEBUG, "EGL DMA-BUF format modifiers %s",
                 has_modifiers ? "supported" : "unsupported");
    }
}

static struct ky_egl *egl_create(void)
{
    /* detect client extensions with EGL_NO_DISPLAY */
    const char *client_exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_exts_str == NULL) {
        if (eglGetError() == EGL_BAD_DISPLAY) {
            kywc_log(KYWC_ERROR, "EGL_EXT_client_extensions not supported");
        } else {
            kywc_log(KYWC_ERROR, "Failed to query EGL client extensions");
        }
        return NULL;
    }

    kywc_log(KYWC_INFO, "Supported EGL client extensions: %s", client_exts_str);

    if (!epoxy_extension_in_string(client_exts_str, "EGL_EXT_platform_base")) {
        kywc_log(KYWC_ERROR, "EGL_EXT_platform_base not supported");
        return NULL;
    }

    struct ky_egl *egl = calloc(1, sizeof(*egl));
    if (egl == NULL) {
        kywc_log_errno(KYWC_ERROR, "Allocation failed");
        return NULL;
    }

    egl->exts.KHR_platform_gbm = epoxy_extension_in_string(client_exts_str, "EGL_KHR_platform_gbm");
    egl->exts.EXT_platform_device =
        epoxy_extension_in_string(client_exts_str, "EGL_EXT_platform_device");
    egl->exts.KHR_display_reference =
        epoxy_extension_in_string(client_exts_str, "EGL_KHR_display_reference");
    /*
     * EGL_EXT_device_base is split in two:
     * EGL_EXT_device_query and EGL_EXT_device_enumeration
     */
    bool has_device_base = epoxy_extension_in_string(client_exts_str, "EGL_EXT_device_base");
    egl->exts.EXT_device_enumeration =
        has_device_base || epoxy_extension_in_string(client_exts_str, "EGL_EXT_device_enumeration");
    egl->exts.EXT_device_query =
        has_device_base || epoxy_extension_in_string(client_exts_str, "EGL_EXT_device_query");

    if (epoxy_extension_in_string(client_exts_str, "EGL_KHR_debug")) {
        static const EGLAttrib debug_attribs[] = {
            EGL_DEBUG_MSG_CRITICAL_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_ERROR_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_WARN_KHR,
            EGL_TRUE,
            EGL_DEBUG_MSG_INFO_KHR,
            EGL_TRUE,
            EGL_NONE,
        };
        eglDebugMessageControlKHR(egl_log, debug_attribs);
    }

    return egl;
}

static bool egl_init_display(struct ky_egl *egl, EGLDisplay display)
{
    egl->display = display;

    EGLint major, minor;
    if (eglInitialize(egl->display, &major, &minor) == EGL_FALSE) {
        kywc_log(KYWC_ERROR, "Failed to initialize EGL");
        return false;
    }

    const char *display_exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
    if (display_exts_str == NULL) {
        kywc_log(KYWC_ERROR, "Failed to query EGL display extensions");
        return false;
    }

    if (!epoxy_extension_in_string(display_exts_str, "EGL_KHR_no_config_context") &&
        !epoxy_extension_in_string(display_exts_str, "EGL_MESA_configless_context")) {
        kywc_log(KYWC_ERROR, "EGL_KHR_no_config_context or "
                             "EGL_MESA_configless_context not supported");
        return false;
    }
    if (!epoxy_extension_in_string(display_exts_str, "EGL_KHR_surfaceless_context")) {
        kywc_log(KYWC_ERROR, "EGL_KHR_surfaceless_context not supported");
        return false;
    }

    const char *device_exts_str = NULL, *driver_name = NULL;
    if (egl->exts.EXT_device_query) {
        EGLAttrib device_attrib;
        if (!eglQueryDisplayAttribEXT(egl->display, EGL_DEVICE_EXT, &device_attrib)) {
            kywc_log(KYWC_ERROR, "eglQueryDisplayAttribEXT(EGL_DEVICE_EXT) failed");
            return false;
        }
        egl->device = (EGLDeviceEXT)device_attrib;

        device_exts_str = eglQueryDeviceStringEXT(egl->device, EGL_EXTENSIONS);
        if (device_exts_str == NULL) {
            kywc_log(KYWC_ERROR, "eglQueryDeviceStringEXT(EGL_EXTENSIONS) failed");
            return false;
        }

        if (epoxy_extension_in_string(device_exts_str, "EGL_MESA_device_software")) {
            char *env = getenv("KYWC_RENDERER_ALLOW_SOFTWARE");
            if (env && strcmp(env, "1") == 0) {
                kywc_log(KYWC_INFO, "Using software rendering");
            } else {
                kywc_log(KYWC_ERROR, "Software rendering detected, please use "
                                     "the KYWC_RENDERER_ALLOW_SOFTWARE environment variable "
                                     "to proceed");
                return false;
            }
        }

        if (epoxy_extension_in_string(device_exts_str, "EGL_EXT_device_persistent_id")) {
            driver_name = eglQueryDeviceStringEXT(egl->device, EGL_DRIVER_NAME_EXT);
        }

        egl->exts.EXT_device_drm = epoxy_extension_in_string(device_exts_str, "EGL_EXT_device_drm");
        egl->exts.EXT_device_drm_render_node =
            epoxy_extension_in_string(device_exts_str, "EGL_EXT_device_drm_render_node");
    }

    egl->exts.KHR_image_base = epoxy_extension_in_string(display_exts_str, "EGL_KHR_image_base");
    egl->exts.EXT_image_dma_buf_import =
        epoxy_extension_in_string(display_exts_str, "EGL_EXT_image_dma_buf_import");
    egl->exts.EXT_image_dma_buf_import_modifiers =
        epoxy_extension_in_string(display_exts_str, "EGL_EXT_image_dma_buf_import_modifiers");

    egl->exts.EXT_create_context_robustness =
        epoxy_extension_in_string(display_exts_str, "EGL_EXT_create_context_robustness");
    egl->exts.IMG_context_priority =
        epoxy_extension_in_string(display_exts_str, "EGL_IMG_context_priority");

    const char *vendor = eglQueryString(egl->display, EGL_VENDOR);
    egl->exts.WL_bind_wayland_display = // only check when ARM mali
        vendor && strcmp(vendor, "ARM") == 0 &&
        epoxy_extension_in_string(display_exts_str, "EGL_WL_bind_wayland_display");

    kywc_log(KYWC_INFO, "Using EGL %d.%d", (int)major, (int)minor);
    kywc_log(KYWC_INFO, "Supported EGL display extensions: %s", display_exts_str);
    if (device_exts_str != NULL) {
        kywc_log(KYWC_INFO, "Supported EGL device extensions: %s", device_exts_str);
    }
    kywc_log(KYWC_INFO, "EGL vendor: %s", vendor);
    if (driver_name != NULL) {
        kywc_log(KYWC_INFO, "EGL driver name: %s", driver_name);
    }

    return true;
}

static void maybe_destroy_context(struct ky_egl *egl)
{
    if (egl->context == EGL_NO_CONTEXT) {
        return;
    }

    ky_egl_unset_current(egl);
    eglDestroyContext(egl->display, egl->context);
    egl->context = EGL_NO_CONTEXT;
}

static bool try_opengl_api(struct ky_egl *egl)
{
    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
        return false;
    }

    size_t atti = 0;
    EGLint attribs[11];

    /* not EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR
     * otherwise we need create VAO and VBO in core profile
     */
    attribs[atti++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
    attribs[atti++] = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
    attribs[atti++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
    attribs[atti++] = 3;
    attribs[atti++] = EGL_CONTEXT_MINOR_VERSION_KHR;
    attribs[atti++] = 2;

    // Request a high priority context if possible
    if (egl->exts.IMG_context_priority) {
        attribs[atti++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
        attribs[atti++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
    }

    if (egl->exts.EXT_create_context_robustness) {
        // not EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT
        attribs[atti++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR;
        attribs[atti++] = EGL_LOSE_CONTEXT_ON_RESET_EXT;
    }

    attribs[atti++] = EGL_NONE;
    assert(atti <= sizeof(attribs) / sizeof(attribs[0]));

    egl->context = eglCreateContext(egl->display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attribs);
    if (egl->context == EGL_NO_CONTEXT) {
        egl->context = eglCreateContext(egl->display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
    }
    if (egl->context == EGL_NO_CONTEXT) {
        return false;
    }

    if (!ky_egl_make_current(egl)) {
        kywc_log(KYWC_ERROR, "Failed to make EGL context current with GL\n");
        maybe_destroy_context(egl);
        return false;
    }

    /* needs at least GL 2.1, if the GL version is less than 2.1,
     * drop the context we created, it's useless.
     */
    int gl_version = epoxy_gl_version();
    if (gl_version < 21) {
        kywc_log(KYWC_ERROR, "GL version is not sufficient (required 21, found %i)", gl_version);
        maybe_destroy_context(egl);
        return false;
    }

    return true;
}

static bool try_gles_api(struct ky_egl *egl)
{
    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        return false;
    }

    size_t atti = 0;
    EGLint attribs[7];

    attribs[atti++] = EGL_CONTEXT_CLIENT_VERSION;
    attribs[atti++] = 2; // opengles 2

    // Request a high priority context if possible
    // Try to reschedule all of our rendering to be completed first. If it
    // fails, it will fallback to the default priority (MEDIUM).
    if (egl->exts.IMG_context_priority) {
        attribs[atti++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
        attribs[atti++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
    }

    if (egl->exts.EXT_create_context_robustness) {
        attribs[atti++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT;
        attribs[atti++] = EGL_LOSE_CONTEXT_ON_RESET_EXT;
    }

    attribs[atti++] = EGL_NONE;
    assert(atti <= sizeof(attribs) / sizeof(attribs[0]));

    egl->context = eglCreateContext(egl->display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attribs);
    if (egl->context == EGL_NO_CONTEXT) {
        kywc_log(KYWC_ERROR, "Failed to create EGL context");
        return false;
    }

    if (!ky_egl_make_current(egl)) {
        kywc_log(KYWC_ERROR, "Failed to make EGL context current with GLES2");
        maybe_destroy_context(egl);
        return false;
    }

    return true;
}

static bool egl_init(struct ky_egl *egl, EGLenum platform, void *remote_display)
{
    EGLint display_attribs[3] = { 0 };
    size_t display_attribs_len = 0;

    if (egl->exts.KHR_display_reference) {
        display_attribs[display_attribs_len++] = EGL_TRACK_REFERENCES_KHR;
        display_attribs[display_attribs_len++] = EGL_TRUE;
    }

    display_attribs[display_attribs_len++] = EGL_NONE;
    assert(display_attribs_len < sizeof(display_attribs) / sizeof(display_attribs[0]));

    EGLDisplay display = eglGetPlatformDisplayEXT(platform, remote_display, display_attribs);
    if (display == EGL_NO_DISPLAY) {
        kywc_log(KYWC_ERROR, "Failed to create EGL display");
        return false;
    }

    if (!egl_init_display(egl, display)) {
        if (egl->exts.KHR_display_reference) {
            eglTerminate(display);
        }
        return false;
    }

    const char *api = getenv("KYWC_RENDERER");
    if (api && strcmp(api, "gl") == 0) {
        if (!try_opengl_api(egl)) {
            kywc_log(KYWC_ERROR, "Cannot use GL by env\n");
            return false;
        }
    } else if (api && strcmp(api, "gles2") == 0) {
        if (!try_gles_api(egl)) {
            kywc_log(KYWC_ERROR, "Cannot use GLES2 by env\n");
            return false;
        }
    } else {
        // try gles2 first
        if (!try_gles_api(egl) && !try_opengl_api(egl)) {
            kywc_log(KYWC_ERROR, "Cannot use neither GL nor GLES2\n");
            return false;
        }
    }

    egl->is_gles = !epoxy_is_desktop_gl();

    if (egl->exts.IMG_context_priority) {
        EGLint priority = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
        eglQueryContext(egl->display, egl->context, EGL_CONTEXT_PRIORITY_LEVEL_IMG, &priority);
        if (priority != EGL_CONTEXT_PRIORITY_HIGH_IMG) {
            kywc_log(KYWC_INFO, "Failed to obtain a high priority context");
        } else {
            kywc_log(KYWC_DEBUG, "Obtained high priority context");
        }
    }

    init_dmabuf_formats(egl);

    return true;
}

static bool device_has_name(const drmDevice *device, const char *name)
{
    for (size_t i = 0; i < DRM_NODE_MAX; i++) {
        if (!(device->available_nodes & (1 << i))) {
            continue;
        }
        if (strcmp(device->nodes[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static EGLDeviceEXT get_egl_device_from_drm_fd(struct ky_egl *egl, int drm_fd)
{
    if (!egl->exts.EXT_device_enumeration) {
        kywc_log(KYWC_DEBUG, "EGL_EXT_device_enumeration not supported");
        return EGL_NO_DEVICE_EXT;
    } else if (!egl->exts.EXT_device_query) {
        kywc_log(KYWC_DEBUG, "EGL_EXT_device_query not supported");
        return EGL_NO_DEVICE_EXT;
    }

    EGLint nb_devices = 0;
    if (!eglQueryDevicesEXT(0, NULL, &nb_devices)) {
        kywc_log(KYWC_ERROR, "Failed to query EGL devices");
        return EGL_NO_DEVICE_EXT;
    }

    EGLDeviceEXT *devices = calloc(nb_devices, sizeof(*devices));
    if (devices == NULL) {
        kywc_log_errno(KYWC_ERROR, "Failed to allocate EGL device list");
        return EGL_NO_DEVICE_EXT;
    }

    if (!eglQueryDevicesEXT(nb_devices, devices, &nb_devices)) {
        kywc_log(KYWC_ERROR, "Failed to query EGL devices");
        return EGL_NO_DEVICE_EXT;
    }

    drmDevice *device = NULL;
    int ret = drmGetDevice(drm_fd, &device);
    if (ret < 0) {
        kywc_log(KYWC_ERROR, "Failed to get DRM device: %s", strerror(-ret));
        return EGL_NO_DEVICE_EXT;
    }

    EGLDeviceEXT egl_device = NULL;
    for (int i = 0; i < nb_devices; i++) {
        const char *egl_device_name = eglQueryDeviceStringEXT(devices[i], EGL_DRM_DEVICE_FILE_EXT);
        if (egl_device_name == NULL) {
            continue;
        }

        if (device_has_name(device, egl_device_name)) {
            kywc_log(KYWC_DEBUG, "Using EGL device %s", egl_device_name);
            egl_device = devices[i];
            break;
        }
    }

    drmFreeDevice(&device);
    free(devices);

    return egl_device;
}

static bool preferred_drm_fd(int drm_fd)
{
    EGLDisplay display = EGL_NO_DISPLAY;
    bool use_drm_fd = false;

    struct gbm_device *gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
        kywc_log(KYWC_ERROR, "Failed to create GBM device");
        goto out;
    }

    display = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm_device, NULL);
    if (display == EGL_NO_DISPLAY) {
        kywc_log(KYWC_ERROR, "Failed to create EGL display on GBM platform");
        goto out;
    }

    if (!eglInitialize(display, NULL, NULL)) {
        kywc_log(KYWC_ERROR, "Failed to initialize EGL on GBM platform");
        goto out;
    }

    const char *vendor = eglQueryString(display, EGL_VENDOR);
    use_drm_fd = vendor && strcmp(vendor, "ARM") == 0;

out:
    if (display != EGL_NO_DISPLAY) {
        eglTerminate(display);
    }
    if (gbm_device) {
        gbm_device_destroy(gbm_device);
    }

    return use_drm_fd;
}

static int open_render_node(int drm_fd)
{
    if (preferred_drm_fd(drm_fd)) {
        kywc_log(KYWC_INFO, "Preferred using drm fd to create GBM");
        return fcntl(drm_fd, F_DUPFD_CLOEXEC, 0);
    }

    char *render_name = drmGetRenderDeviceNameFromFd(drm_fd);
    if (render_name == NULL) {
        // This can happen on split render/display platforms, fallback to primary node
        render_name = drmGetPrimaryDeviceNameFromFd(drm_fd);
        if (render_name == NULL) {
            kywc_log_errno(KYWC_ERROR, "drmGetPrimaryDeviceNameFromFd failed");
            return -1;
        }
        kywc_log(KYWC_DEBUG, "DRM device '%s' has no render node, falling back to primary node",
                 render_name);
    }

    int render_fd = open(render_name, O_RDWR | O_CLOEXEC);
    if (render_fd < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to open DRM node '%s'", render_name);
    }
    free(render_name);
    return render_fd;
}

struct ky_egl *ky_egl_create_with_drm_fd(int drm_fd)
{
    struct ky_egl *egl = egl_create();
    if (!egl) {
        kywc_log(KYWC_ERROR, "Failed to create EGL context");
        return NULL;
    }

    int gbm_fd = open_render_node(drm_fd);
    if (gbm_fd < 0) {
        kywc_log(KYWC_ERROR, "Failed to open DRM render node");
        goto error;
    }
    egl->gbm_device = gbm_create_device(gbm_fd);
    if (!egl->gbm_device) {
        close(gbm_fd);
        kywc_log(KYWC_ERROR, "Failed to create GBM device");
        goto error;
    }

    if (egl->exts.EXT_platform_device) {
        /*
         * Search for the EGL device matching the DRM fd using the
         * EXT_device_enumeration extension.
         */
        EGLDeviceEXT egl_device = get_egl_device_from_drm_fd(egl, drm_fd);
        if (egl_device != EGL_NO_DEVICE_EXT) {
            if (egl_init(egl, EGL_PLATFORM_DEVICE_EXT, egl_device)) {
                kywc_log(KYWC_DEBUG, "Using EGL_PLATFORM_DEVICE_EXT");
                return egl;
            }
            goto error;
        }
        /* Falls back on GBM in case the device was not found */
    } else {
        kywc_log(KYWC_DEBUG, "EXT_platform_device not supported");
    }

    if (egl->exts.KHR_platform_gbm) {
        if (egl_init(egl, EGL_PLATFORM_GBM_KHR, egl->gbm_device)) {
            kywc_log(KYWC_DEBUG, "Using EGL_PLATFORM_GBM_KHR");
            return egl;
        }

    } else {
        kywc_log(KYWC_DEBUG, "KHR_platform_gbm not supported");
    }

error:
    kywc_log(KYWC_ERROR, "Failed to initialize EGL context");
    gbm_device_destroy(egl->gbm_device);
    close(gbm_fd);
    free(egl);
    eglReleaseThread();
    return NULL;
}

void ky_egl_destroy(struct ky_egl *egl)
{
    if (egl == NULL) {
        return;
    }

    wlr_drm_format_set_finish(&egl->dmabuf_texture_formats);
    wlr_drm_format_set_finish(&egl->dmabuf_render_formats);
    wlr_drm_format_set_finish(&egl->dmabuf_render_single_plane_formats);

    eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl->display, egl->context);

    if (egl->exts.KHR_display_reference) {
        eglTerminate(egl->display);
    }

    eglReleaseThread();

    if (egl->gbm_device) {
        int gbm_fd = gbm_device_get_fd(egl->gbm_device);
        gbm_device_destroy(egl->gbm_device);
        close(gbm_fd);
    }

    free(egl);
}

bool ky_egl_destroy_image(struct ky_egl *egl, EGLImageKHR image)
{
    if (!egl->exts.KHR_image_base) {
        return false;
    }
    if (!image) {
        return true;
    }
    return eglDestroyImageKHR(egl->display, image);
}

bool ky_egl_make_current(struct ky_egl *egl)
{
    if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl->context)) {
        kywc_log(KYWC_ERROR, "eglMakeCurrent failed");
        return false;
    }
    return true;
}

bool ky_egl_unset_current(struct ky_egl *egl)
{
    if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        kywc_log(KYWC_ERROR, "eglMakeCurrent failed");
        return false;
    }
    return true;
}

bool ky_egl_is_current(struct ky_egl *egl)
{
    return eglGetCurrentContext() == egl->context;
}

void ky_egl_save_context(struct ky_egl_context *context)
{
    context->display = eglGetCurrentDisplay();
    context->context = eglGetCurrentContext();
    context->draw_surface = eglGetCurrentSurface(EGL_DRAW);
    context->read_surface = eglGetCurrentSurface(EGL_READ);
}

bool ky_egl_restore_context(struct ky_egl_context *context)
{
    // If the saved context is a null-context, we must use the current
    // display instead of the saved display because eglMakeCurrent() can't
    // handle EGL_NO_DISPLAY.
    EGLDisplay display =
        context->display == EGL_NO_DISPLAY ? eglGetCurrentDisplay() : context->display;

    // If the current display is also EGL_NO_DISPLAY, we assume that there
    // is currently no context set and no action needs to be taken to unset
    // the context.
    if (display == EGL_NO_DISPLAY) {
        return true;
    }

    return eglMakeCurrent(display, context->draw_surface, context->read_surface, context->context);
}

EGLImageKHR ky_egl_create_image_from_dmabuf(struct ky_egl *egl,
                                            struct wlr_dmabuf_attributes *attributes,
                                            bool *external_only)
{
    if (!egl->exts.KHR_image_base || !egl->exts.EXT_image_dma_buf_import) {
        kywc_log(KYWC_ERROR, "dmabuf import extension not present");
        return NULL;
    }

    if (attributes->modifier != DRM_FORMAT_MOD_INVALID &&
        attributes->modifier != DRM_FORMAT_MOD_LINEAR && !egl->has_modifiers) {
        kywc_log(KYWC_ERROR, "EGL implementation doesn't support modifiers");
        return NULL;
    }

    unsigned int atti = 0;
    EGLint attribs[50];
    attribs[atti++] = EGL_WIDTH;
    attribs[atti++] = attributes->width;
    attribs[atti++] = EGL_HEIGHT;
    attribs[atti++] = attributes->height;
    attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[atti++] = attributes->format;

    struct {
        EGLint fd;
        EGLint offset;
        EGLint pitch;
        EGLint mod_lo;
        EGLint mod_hi;
    } attr_names[WLR_DMABUF_MAX_PLANES] = {
        { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE0_PITCH_EXT,
          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT },
        { EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
          EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT },
        { EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT,
          EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT },
        { EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT,
          EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT }
    };

    for (int i = 0; i < attributes->n_planes; i++) {
        attribs[atti++] = attr_names[i].fd;
        attribs[atti++] = attributes->fd[i];
        attribs[atti++] = attr_names[i].offset;
        attribs[atti++] = attributes->offset[i];
        attribs[atti++] = attr_names[i].pitch;
        attribs[atti++] = attributes->stride[i];
        if (egl->has_modifiers && attributes->modifier != DRM_FORMAT_MOD_INVALID) {
            attribs[atti++] = attr_names[i].mod_lo;
            attribs[atti++] = attributes->modifier & 0xFFFFFFFF;
            attribs[atti++] = attr_names[i].mod_hi;
            attribs[atti++] = attributes->modifier >> 32;
        }
    }

    // Our clients don't expect our usage to trash the buffer contents
    attribs[atti++] = EGL_IMAGE_PRESERVED_KHR;
    attribs[atti++] = EGL_TRUE;

    attribs[atti++] = EGL_NONE;
    assert(atti < sizeof(attribs) / sizeof(attribs[0]));

    EGLImageKHR image =
        eglCreateImageKHR(egl->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
    if (image == EGL_NO_IMAGE_KHR) {
        kywc_log(KYWC_ERROR, "eglCreateImageKHR failed");
        return EGL_NO_IMAGE_KHR;
    }

    *external_only = !wlr_drm_format_set_has(&egl->dmabuf_render_formats, attributes->format,
                                             attributes->modifier);
    return image;
}

static char *get_render_name(const char *name)
{
    uint32_t flags = 0;
    int devices_len = drmGetDevices2(flags, NULL, 0);
    if (devices_len < 0) {
        kywc_log(KYWC_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
        return NULL;
    }
    drmDevice **devices = calloc(devices_len, sizeof(drmDevice *));
    if (devices == NULL) {
        kywc_log_errno(KYWC_ERROR, "Allocation failed");
        return NULL;
    }
    devices_len = drmGetDevices2(flags, devices, devices_len);
    if (devices_len < 0) {
        free(devices);
        kywc_log(KYWC_ERROR, "drmGetDevices2 failed: %s", strerror(-devices_len));
        return NULL;
    }

    const drmDevice *match = NULL;
    for (int i = 0; i < devices_len; i++) {
        if (device_has_name(devices[i], name)) {
            match = devices[i];
            break;
        }
    }

    char *render_name = NULL;
    if (match == NULL) {
        kywc_log(KYWC_ERROR, "Cannot find DRM device %s", name);
    } else if (!(match->available_nodes & (1 << DRM_NODE_RENDER))) {
        // Likely a split display/render setup. Pick the primary node and hope
        // Mesa will open the right render node under-the-hood.
        kywc_log(KYWC_DEBUG,
                 "DRM device %s has no render node, "
                 "falling back to primary node",
                 name);
        assert(match->available_nodes & (1 << DRM_NODE_PRIMARY));
        render_name = strdup(match->nodes[DRM_NODE_PRIMARY]);
    } else {
        render_name = strdup(match->nodes[DRM_NODE_RENDER]);
    }

    for (int i = 0; i < devices_len; i++) {
        drmFreeDevice(&devices[i]);
    }
    free(devices);

    return render_name;
}

static int dup_egl_device_drm_fd(struct ky_egl *egl)
{
    if (egl->device == EGL_NO_DEVICE_EXT ||
        (!egl->exts.EXT_device_drm && !egl->exts.EXT_device_drm_render_node)) {
        return -1;
    }

    char *render_name = NULL;
    if (egl->exts.EXT_device_drm_render_node) {
        const char *name = eglQueryDeviceStringEXT(egl->device, EGL_DRM_RENDER_NODE_FILE_EXT);
        if (name == NULL) {
            kywc_log(KYWC_DEBUG, "EGL device has no render node");
            return -1;
        }
        render_name = strdup(name);
    }

    if (render_name == NULL) {
        const char *primary_name = eglQueryDeviceStringEXT(egl->device, EGL_DRM_DEVICE_FILE_EXT);
        if (primary_name == NULL) {
            kywc_log(KYWC_ERROR, "eglQueryDeviceStringEXT(EGL_DRM_DEVICE_FILE_EXT) failed");
            return -1;
        }

        render_name = get_render_name(primary_name);
        if (render_name == NULL) {
            kywc_log(KYWC_ERROR, "Can't find render node name for device %s", primary_name);
            return -1;
        }
    }

    int render_fd = open(render_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (render_fd < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to open DRM render node %s", render_name);
        free(render_name);
        return -1;
    }
    free(render_name);

    return render_fd;
}

int ky_egl_dup_drm_fd(struct ky_egl *egl)
{
    int fd = dup_egl_device_drm_fd(egl);
    if (fd >= 0) {
        return fd;
    }

    // Fallback to GBM's FD if we can't use EGLDevice
    if (egl->gbm_device == NULL) {
        return -1;
    }

    fd = fcntl(gbm_device_get_fd(egl->gbm_device), F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        kywc_log_errno(KYWC_ERROR, "Failed to dup GBM FD");
    }
    return fd;
}
