// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdrm/drm_fourcc.h>
#include <sys/mman.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-egl.h>

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "buffer.h"

struct kywc_buffer_helper {
    kywc_context *ctx;
    struct wl_list buffers;

    EGLDisplay display;
    EGLConfig config;
    EGLContext context;

    struct {
        bool KHR_image_base;
        bool EXT_image_dma_buf_import;
        bool EXT_image_dma_buf_import_modifiers;
        bool EXT_platform_wayland;
        bool OES_egl_image_external;
    } exts;

    struct {
        PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
        PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
        PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    } procs;

    struct wl_display *wl_display;
    struct wl_compositor *wl_compositor;
    struct xdg_wm_base *xdg_wm_base;
    struct zxdg_decoration_manager_v1 *xdg_deco_manager;

    GLuint shader_program;
    GLint tex;
};

struct kywc_window {
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct zxdg_toplevel_decoration_v1 *xdg_deco;

    struct wl_egl_window *native;
    EGLSurface surface;
    struct kywc_buffer *buffer;
};

struct kywc_buffer {
    struct kywc_buffer_helper *helper;
    struct wl_list link;

    void *ptr;
    size_t size;

    EGLImageKHR image;
    GLuint tex, fbo;

    kywc_thumbnail *thumbnail;
    struct kywc_thumbnail_buffer buffer;

    struct kywc_window *window;
};

static const char *vertex_source = "\
precision highp float;\
attribute vec4 position;\
varying vec2 texcoord;\
\
void main() {\
  gl_Position = vec4(position.xy, 0, 1);\
  texcoord = position.zw;\
}\
";

static const char *fragment_source = "\
precision highp float;\
varying vec2 texcoord;\
uniform sampler2D tex;\
\
void main() {\
  gl_FragColor = texture2D(tex, texcoord);\
}\
";

// clang-format off
static const float vertices[] = {
    -1.f,  1.f, 0.f, 0.f,
    -1.f, -1.f, 0.f, 1.f,
    1.f, -1.f, 1.f, 1.f,
    1.f, 1.f, 1.f, 0.f,
};
// clang-format on

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
                                   const char *interface, uint32_t version)
{
    struct kywc_buffer_helper *helper = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        helper->wl_compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        helper->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(helper->xdg_wm_base, &xdg_wm_base_listener, helper);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        helper->xdg_deco_manager =
            wl_registry_bind(registry, id, &zxdg_decoration_manager_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    // do nothing
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void window_helper_init(struct kywc_buffer_helper *helper)
{
    struct wl_registry *registry = wl_display_get_registry(helper->wl_display);
    wl_registry_add_listener(registry, &registry_listener, helper);
    wl_display_roundtrip(helper->wl_display);
    wl_registry_destroy(registry);

    if (!helper->xdg_wm_base) {
        return;
    }

    /* build shaders for texture */
    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, helper->context);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_source, NULL);
    glCompileShader(vertex_shader);

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_source, NULL);
    glCompileShader(fragment_shader);

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint ok;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        fprintf(stderr, "Failed to link shader\n");
        glDeleteProgram(shader_program);
        return;
    }

    glBindAttribLocation(shader_program, 0, "position");
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    helper->shader_program = shader_program;
    helper->tex = glGetUniformLocation(shader_program, "tex");

    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static void kywc_window_draw(struct kywc_window *window)
{
    struct kywc_buffer_helper *helper = window->buffer->helper;

    eglMakeCurrent(helper->display, window->surface, window->surface, helper->context);

    glUseProgram(helper->shader_program);
    glEnableVertexAttribArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window->buffer->tex);
    glUniform1i(helper->tex, 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    eglSwapBuffers(helper->display, window->surface);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width,
                                   int32_t height, struct wl_array *states)
{
    if (width == 0 || height == 0) {
        return;
    }

    struct kywc_window *window = data;
    wl_egl_window_resize(window->native, width, height, 0, 0);
    glViewport(0, 0, width, height);
    kywc_window_draw(window);
}

static void kywc_window_destroy(struct kywc_window *window)
{
    EGLDisplay display = window->buffer->helper->display;

    eglDestroySurface(display, window->surface);
    wl_egl_window_destroy(window->native);
    zxdg_toplevel_decoration_v1_destroy(window->xdg_deco);
    xdg_toplevel_destroy(window->xdg_toplevel);
    xdg_surface_destroy(window->xdg_surface);

    window->buffer->window = NULL;
    free(window);
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct kywc_window *window = data;
    kywc_window_destroy(window);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void toplevel_decoration_configure(
    void *data, struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration_v1, uint32_t mode)
{
    // do nothing
}

static const struct zxdg_toplevel_decoration_v1_listener xdg_toplevel_decoration_listener = {
    .configure = toplevel_decoration_configure,
};

static struct kywc_window *kywc_window_create(struct kywc_buffer *buffer, const char *title)
{
    struct kywc_buffer_helper *helper = buffer->helper;
    if (!helper->xdg_wm_base) {
        return NULL;
    }
    struct kywc_window *window = calloc(1, sizeof(*window));
    if (!window) {
        return NULL;
    }

    window->wl_surface = wl_compositor_create_surface(helper->wl_compositor);
    window->xdg_surface = xdg_wm_base_get_xdg_surface(helper->xdg_wm_base, window->wl_surface);
    xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);

    window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);
    window->xdg_deco = zxdg_decoration_manager_v1_get_toplevel_decoration(helper->xdg_deco_manager,
                                                                          window->xdg_toplevel);
    zxdg_toplevel_decoration_v1_add_listener(window->xdg_deco, &xdg_toplevel_decoration_listener,
                                             window);
    zxdg_toplevel_decoration_v1_set_mode(window->xdg_deco,
                                         ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    xdg_toplevel_set_app_id(window->xdg_toplevel, "kywc-thumbnail");
    xdg_toplevel_set_title(window->xdg_toplevel, title);
    wl_surface_commit(window->wl_surface);

    int width = buffer->buffer.width / 2;
    int height = buffer->buffer.height / 2;
    window->native = wl_egl_window_create(window->wl_surface, width, height);
    window->surface = eglCreateWindowSurface(helper->display, helper->config,
                                             (EGLNativeWindowType)window->native, NULL);
    wl_display_roundtrip(helper->wl_display);

    window->buffer = buffer;
    return window;
}

static bool check_ext(const char *exts, const char *ext)
{
    size_t extlen = strlen(ext);
    const char *end = exts + strlen(exts);

    while (exts < end) {
        if (*exts == ' ') {
            exts++;
            continue;
        }
        size_t n = strcspn(exts, " ");
        if (n == extlen && strncmp(ext, exts, n) == 0) {
            return true;
        }
        exts += n;
    }
    return false;
}

static void load_proc(void *proc_ptr, const char *name)
{
    void *proc = (void *)eglGetProcAddress(name);
    if (proc == NULL) {
        fprintf(stderr, "eglGetProcAddress(%s) failed\n", name);
        abort();
    }
    *(void **)proc_ptr = proc;
}

static void dmabuf_helper_init(struct kywc_buffer_helper *helper)
{
    const char *client_exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_exts_str == NULL) {
        if (eglGetError() == EGL_BAD_DISPLAY) {
            fprintf(stderr, "EGL_EXT_client_extensions not supported\n");
        } else {
            fprintf(stderr, "Failed to query EGL client extensions\n");
        }
        return;
    }

    // fprintf(stdout, "Supported EGL client extensions: %s\n", client_exts_str);

    if (!check_ext(client_exts_str, "EGL_EXT_platform_base")) {
        fprintf(stderr, "EGL_EXT_platform_base not supported\n");
        return;
    }

    if (check_ext(client_exts_str, "EGL_EXT_platform_wayland")) {
        helper->exts.EXT_platform_wayland = true;
        load_proc(&helper->procs.eglGetPlatformDisplayEXT, "eglGetPlatformDisplayEXT");
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        fprintf(stderr, "Failed to bind to the OpenGL ES API\n");
        return;
    }

    EGLDisplay egl_display = EGL_NO_DISPLAY;
    /* get egl display */
    if (helper->exts.EXT_platform_wayland) {
        egl_display = helper->procs.eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT,
                                                             helper->wl_display, NULL);
    } else {
        egl_display = eglGetDisplay(helper->wl_display);
    }

    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "egl get display failed\n");
        return;
    }

    EGLint major, minor;
    if (eglInitialize(egl_display, &major, &minor) == EGL_FALSE) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return;
    }

    const char *display_exts_str = eglQueryString(egl_display, EGL_EXTENSIONS);
    if (display_exts_str == NULL) {
        fprintf(stderr, "Failed to query EGL display extensions\n");
        return;
    }

    if (check_ext(display_exts_str, "EGL_KHR_image_base")) {
        helper->exts.KHR_image_base = true;
        load_proc(&helper->procs.eglCreateImageKHR, "eglCreateImageKHR");
        load_proc(&helper->procs.eglDestroyImageKHR, "eglDestroyImageKHR");
    }

    helper->exts.EXT_image_dma_buf_import =
        check_ext(display_exts_str, "EGL_EXT_image_dma_buf_import");
    if (check_ext(display_exts_str, "EGL_EXT_image_dma_buf_import_modifiers")) {
        helper->exts.EXT_image_dma_buf_import_modifiers = true;
    }

    // fprintf(stdout, "Using EGL %d.%d\n", (int)major, (int)minor);
    // fprintf(stdout, "Supported EGL display extensions: %s\n", display_exts_str);
    // fprintf(stdout, "EGL vendor: %s\n", eglQueryString(helper->display, EGL_VENDOR));

    // clang-format off
    EGLint config_attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  EGL_NONE
    };
    // clang-format on
    EGLint num_configs = 0;
    if (!eglChooseConfig(egl_display, config_attribs, &helper->config, 1, &num_configs) ||
        !num_configs) {
        fprintf(stderr, "Failed to choose a config\n");
        return;
    }

    /* using opengles 2 */
    const EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext egl_context = eglCreateContext(egl_display, helper->config, EGL_NO_CONTEXT, attribs);
    if (egl_display == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return;
    }

    if (!eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        eglDestroyContext(egl_display, egl_context);
        return;
    }

    const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
    if (exts_str == NULL) {
        fprintf(stderr, "Failed to get GL_EXTENSIONS\n");
        eglDestroyContext(egl_display, egl_context);
        return;
    }

    if (check_ext(exts_str, "GL_OES_EGL_image_external")) {
        helper->exts.OES_egl_image_external = true;
        load_proc(&helper->procs.glEGLImageTargetTexture2DOES, "glEGLImageTargetTexture2DOES");
    }

    // fprintf(stdout, "Using %s\n", glGetString(GL_VERSION));
    // fprintf(stdout, "GL vendor: %s\n", glGetString(GL_VENDOR));
    // fprintf(stdout, "GL renderer: %s\n", glGetString(GL_RENDERER));
    // fprintf(stdout, "Supported GLES2 extensions: %s\n", exts_str);

    helper->display = egl_display;
    helper->context = egl_context;
    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

struct kywc_buffer_helper *kywc_buffer_helper_create(kywc_context *ctx)
{
    struct kywc_buffer_helper *helper = calloc(1, sizeof(*helper));
    if (helper == NULL) {
        fprintf(stderr, "Allocation failed\n");
        return NULL;
    }

    helper->ctx = ctx;
    helper->wl_display = kywc_context_get_display(ctx);
    wl_list_init(&helper->buffers);

    dmabuf_helper_init(helper);
    if (helper->display) {
        window_helper_init(helper);
    }

    return helper;
}

void kywc_buffer_helper_destroy(struct kywc_buffer_helper *helper)
{
    if (helper == NULL) {
        return;
    }

    struct kywc_buffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &helper->buffers, link) {
        kywc_buffer_destroy(buffer);
    }

    if (helper->xdg_wm_base) {
        xdg_wm_base_destroy(helper->xdg_wm_base);
        wl_compositor_destroy(helper->wl_compositor);
        wl_display_flush(helper->wl_display);
    }

    if (helper->display) {
        eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(helper->display, helper->context);
        eglReleaseThread();
    }

    free(helper);
}

#define ADD_ATTRIB(name, value)                                                                    \
    do {                                                                                           \
        attribs[num_attribs++] = (name);                                                           \
        attribs[num_attribs++] = (value);                                                          \
        attribs[num_attribs] = EGL_NONE;                                                           \
    } while (0)

static bool kywc_buffer_import_dmabuf(struct kywc_buffer *kywc_buffer,
                                      const struct kywc_thumbnail_buffer *buffer, bool can_reuse)
{
    struct kywc_buffer_helper *helper = kywc_buffer->helper;

    if (!helper->exts.KHR_image_base || !helper->exts.EXT_image_dma_buf_import) {
        fprintf(stderr, "dma_buf_import is not support\n");
        return false;
    }

    if (buffer->modifier != DRM_FORMAT_MOD_INVALID && buffer->modifier != DRM_FORMAT_MOD_LINEAR &&
        !helper->exts.EXT_image_dma_buf_import_modifiers) {
        fprintf(stderr, "buffer has a modifier when dma_buf_import_modifiers is not supported\n");
        return false;
    }

    if (kywc_buffer->image) {
        if (can_reuse) {
            return true;
        }
        /* release prev buffer stuff */
        eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, helper->context);
        glDeleteFramebuffers(1, &kywc_buffer->fbo);
        glDeleteTextures(1, &kywc_buffer->tex);
        helper->procs.eglDestroyImageKHR(helper->display, kywc_buffer->image);
        eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    EGLint attribs[20] = { EGL_NONE };
    int num_attribs = 0;

    ADD_ATTRIB(EGL_WIDTH, buffer->width);
    ADD_ATTRIB(EGL_HEIGHT, buffer->height);
    ADD_ATTRIB(EGL_LINUX_DRM_FOURCC_EXT, buffer->format);
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_FD_EXT, buffer->fd);
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_OFFSET_EXT, buffer->offset);
    ADD_ATTRIB(EGL_DMA_BUF_PLANE0_PITCH_EXT, buffer->stride);
    if (buffer->modifier != DRM_FORMAT_MOD_INVALID) {
        ADD_ATTRIB(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, buffer->modifier & 0xFFFFFFFF);
        ADD_ATTRIB(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, buffer->modifier >> 32);
    }
    ADD_ATTRIB(EGL_IMAGE_PRESERVED_KHR, EGL_TRUE);

    kywc_buffer->image = helper->procs.eglCreateImageKHR(helper->display, EGL_NO_CONTEXT,
                                                         EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
    if (kywc_buffer->image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "eglCreateImageKHR failed\n");
        return false;
    }

    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, helper->context);

    glGenTextures(1, &kywc_buffer->tex);
    glBindTexture(GL_TEXTURE_2D, kywc_buffer->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    helper->procs.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, kywc_buffer->image);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &kywc_buffer->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, kywc_buffer->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, kywc_buffer->tex,
                           0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    return true;
}

static bool kywc_buffer_export_dmabuf(struct kywc_buffer *buffer, void *data)
{
    struct kywc_buffer_helper *helper = buffer->helper;

    if (!eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, helper->context)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, buffer->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, buffer->buffer.width, buffer->buffer.height, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                 data);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    return true;
}

static bool kywc_buffer_import_memfd(struct kywc_buffer *kywc_buffer,
                                     const struct kywc_thumbnail_buffer *buffer, bool can_reuse)
{
    if (kywc_buffer->ptr) {
        if (can_reuse) {
            return true;
        }
        munmap(kywc_buffer->ptr, kywc_buffer->size);
    }

    kywc_buffer->size = buffer->height * buffer->stride + buffer->offset;
    kywc_buffer->ptr = mmap(0, kywc_buffer->size, PROT_READ, MAP_PRIVATE, buffer->fd, 0);
    if (kywc_buffer->ptr == MAP_FAILED) {
        kywc_buffer->ptr = NULL;
        return false;
    }

    return true;
}

static bool kywc_buffer_export_memfd(struct kywc_buffer *buffer, void *data)
{
    char *src = (char *)buffer->ptr + buffer->buffer.offset;
    size_t line = buffer->buffer.width * 4;

    /* check if can use one memcpy */
    if (buffer->buffer.stride == line) {
        memcpy(data, src, buffer->size - buffer->buffer.offset);
        return true;
    }

    char *dst = data;
    for (uint32_t i = 0; i < buffer->buffer.height; i++) {
        memcpy(dst, src + i * buffer->buffer.stride, line);
        dst += line;
    }

    return true;
}

static struct kywc_buffer *helper_get_buffer(struct kywc_buffer_helper *helper,
                                             kywc_thumbnail *thumbnail)
{
    struct kywc_buffer *buffer;
    wl_list_for_each(buffer, &helper->buffers, link) {
        if (buffer->thumbnail == thumbnail) {
            return buffer;
        }
    }
    return NULL;
}

struct kywc_buffer *kywc_buffer_hepler_import_thumbnail(struct kywc_buffer_helper *helper,
                                                        kywc_thumbnail *thumbnail,
                                                        const struct kywc_thumbnail_buffer *buffer)
{
    if (helper == NULL || buffer == NULL) {
        return NULL;
    }

    struct kywc_buffer *kywc_buffer = helper_get_buffer(helper, thumbnail);
    if (!kywc_buffer) {
        kywc_buffer = calloc(1, sizeof(*kywc_buffer));
        if (kywc_buffer == NULL) {
            fprintf(stderr, "Allocation failed\n");
            return NULL;
        }
        kywc_buffer->helper = helper;
        kywc_buffer->thumbnail = thumbnail;
        wl_list_insert(&helper->buffers, &kywc_buffer->link);
    }

    bool success = false;
    bool can_reuse = buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_REUSED;
    /* try egl dambuf import if support */
    if (helper->display && (buffer->flags & KYWC_THUMBNAIL_BUFFER_IS_DMABUF)) {
        success = kywc_buffer_import_dmabuf(kywc_buffer, buffer, can_reuse);
    }
    /* fallback to mmap */
    if (!success) {
        success = kywc_buffer_import_memfd(kywc_buffer, buffer, can_reuse);
    }

    if (!success) {
        free(kywc_buffer);
        return NULL;
    }

    kywc_buffer->buffer = *buffer;
    return kywc_buffer;
}

void kywc_buffer_destroy(struct kywc_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    wl_list_remove(&buffer->link);

    if (buffer->window) {
        kywc_window_destroy(buffer->window);
    }

    if (buffer->image) {
        struct kywc_buffer_helper *helper = buffer->helper;
        eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, helper->context);
        glDeleteFramebuffers(1, &buffer->fbo);
        glDeleteTextures(1, &buffer->tex);
        helper->procs.eglDestroyImageKHR(helper->display, buffer->image);
        eglMakeCurrent(helper->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (buffer->ptr) {
        munmap(buffer->ptr, buffer->size);
    }

    free(buffer);
}

bool kywc_buffer_write_to_file(struct kywc_buffer *buffer, const char *path)
{
    size_t size = buffer->buffer.width * buffer->buffer.height * 4;
    unsigned char *data = malloc(size);
    if (!data) {
        return false;
    }

    bool success = false;
    if (buffer->image) {
        success = kywc_buffer_export_dmabuf(buffer, data);
    } else if (buffer->ptr) {
        success = kywc_buffer_export_memfd(buffer, data);
    }
    if (!success) {
        free(data);
        return false;
    }

    FILE *fp = fopen(path, "w+");
    if (!fp) {
        free(data);
        fprintf(stderr, "failed to open cache file %s\n", path);
        return false;
    }

    fwrite(data, 1, size, fp);
    fflush(fp);
    fclose(fp);
    free(data);

    return true;
}

bool kywc_buffer_show_in_window(struct kywc_buffer *buffer, const char *title)
{
    struct kywc_buffer_helper *helper = buffer->helper;
    /* only support dmabuf now */
    if (!helper->xdg_wm_base || !buffer->image) {
        return false;
    }

    if (!buffer->window) {
        buffer->window = kywc_window_create(buffer, title);
        if (!buffer->window) {
            return false;
        }
    }

    kywc_window_draw(buffer->window);

    return true;
}
