#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include "internal.h"
#include "gles2.h"
#include "render.h"
#include "platform/context/egl.h"
#include "platform/context/context.h"
#include "compositor/view.h"
#include "xwayland/xwm.h"
#include "resources/types/surface.h"
#include "resources/types/xdg-surface.h"
#include "resources/types/buffer.h"

static bool DRAW_OPAQUE = false;

static const GLubyte cursor_palette[];

enum program_type {
   PROGRAM_RGB,
   PROGRAM_RGBA,
   PROGRAM_EGL,
   PROGRAM_Y_UV,
   PROGRAM_Y_U_V,
   PROGRAM_Y_XUXV,
   PROGRAM_CURSOR,
   PROGRAM_LAST,
};

enum {
   UNIFORM_TEXTURE0,
   UNIFORM_TEXTURE1,
   UNIFORM_TEXTURE2,
   UNIFORM_RESOLUTION,
   UNIFORM_LAST,
};

enum {
   TEXTURE_BLACK,
   TEXTURE_CURSOR,
   TEXTURE_FAKEFB,
   TEXTURE_LAST
};

static const char *uniform_names[UNIFORM_LAST] = {
   "texture0",
   "texture1",
   "texture2",
   "resolution",
};

static const struct {
   GLenum format;
   GLenum type;
} format_map[] = {
   { GL_RGBA, GL_UNSIGNED_BYTE }, // WLC_RGBA8888
};

struct ctx {
   const char *extensions;

   struct ctx_program *program;

   struct ctx_program {
      GLuint obj;
      GLuint uniforms[UNIFORM_LAST];
   } programs[PROGRAM_LAST];

   struct wlc_size resolution, mode;
   GLuint textures[TEXTURE_LAST];
   GLuint clear_fbo;
   GLenum internal_format;
   GLenum preferred_type;
   bool fakefb_dirty;

   struct {
      PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   } api;
};

struct paint {
   struct wlc_geometry visible;
   enum program_type program;
   bool filter;
};

static const char*
gl_error_string(const GLenum error)
{
   switch (error) {
      case GL_INVALID_ENUM:
         return "GL_INVALID_ENUM";
      case GL_INVALID_VALUE:
         return "GL_INVALID_VALUE";
      case GL_INVALID_OPERATION:
         return "GL_INVALID_OPERATION";
      case GL_OUT_OF_MEMORY:
         return "GL_OUT_OF_MEMORY";
   }

   return "UNKNOWN GL ERROR";
}

static
void
gl_call(const char *func, uint32_t line, const char *glfunc)
{
   GLenum error;
   if ((error = glGetError()) == GL_NO_ERROR)
      return;

   wlc_log(WLC_LOG_ERROR, "gles2: function %s at line %u: %s == %s", func, line, glfunc, gl_error_string(error));
}

#ifndef __STRING
#  define __STRING(x) #x
#endif

#define GL_CALL(x) x; gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

WLC_PURE static bool
has_extension(const struct ctx *context, const char *extension)
{
   assert(context && extension);

   if (!context->extensions)
      return false;

   size_t len = strlen(extension), pos;
   const char *s = context->extensions;
   while ((pos = strcspn(s, " ")) != 0) {
      size_t next = pos + (s[pos] != 0);

      if (chck_cstrneq(s, extension, len))
         return true;

      s += next;
   }

   return false;
}

static void
set_program(struct ctx *context, enum program_type type)
{
   assert(context && type >= 0 && type < PROGRAM_LAST);

   context->program = &context->programs[type];
   GL_CALL(glUseProgram(context->program->obj));
}

static GLuint
create_shader(const char *source, GLenum shader_type)
{
   assert(source);

   GLuint shader = glCreateShader(shader_type);
   assert(shader != 0);

   GL_CALL(glShaderSource(shader, 1, (const char**)&source, NULL));
   GL_CALL(glCompileShader(shader));

   GLint status;
   GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &status));
   if (!status) {
      GLsizei len;
      char log[1024];
      GL_CALL(glGetShaderInfoLog(shader, sizeof(log), &len, log));
      wlc_log(WLC_LOG_ERROR, "Compiling %s: %*s\n", (shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment"), len, log);
      abort();
   }

   return shader;
}

static struct ctx*
create_context(void)
{
   const char *vert_shader =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform vec2 resolution;\n"
      "attribute vec4 pos;\n"
      "attribute vec2 uv;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  mat4 ortho = mat4("
      "    2.0/resolution.x,         0,          0, 0,"
      "            0,        -2.0/resolution.y,  0, 0,"
      "            0,                0,         -1, 0,"
      "           -1,                1,          0, 1"
      "  );\n"
      "  gl_Position = ortho * pos;\n"
      "  v_uv = uv;\n"
      "}\n";

   const char *frag_shader_dummy =
      "#version 100\n"
      "precision mediump float;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";

   const char *frag_shader_cursor =
      "#version 100\n"
      "precision highp float;\n"
      "uniform sampler2D texture0;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec4 palette[3];\n"
      "  palette[0] = vec4(0.0, 0.0, 0.0, 1.0);\n"
      "  palette[1] = vec4(1.0, 1.0, 1.0, 1.0);\n"
      "  palette[2] = vec4(0.0, 0.0, 0.0, 0.0);\n"
      "  gl_FragColor = palette[int(texture2D(texture0, v_uv).r * 256.0)];\n"
      "}\n";

   const char *frag_shader_rgb =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(texture2D(texture0, v_uv).rgb, 1.0);\n"
      "}\n";

   const char *frag_shader_rgba =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec4 col = texture2D(texture0, v_uv);\n"
      "  gl_FragColor = vec4(col.rgb, col.a);\n"
      "}\n";

   const char *frag_shader_egl =
      "#version 100\n"
      "#extension GL_OES_EGL_image_external : require\n"
      "precision mediump float;\n"
      "uniform samplerExternalOES texture0;\n"
      "varying vec2 v_uv;\n"
      "void main()\n"
      "{\n"
      "  vec4 col = texture2D(texture0, v_uv);\n"
      "  gl_FragColor = vec4(col.rgb, col.a)\n;"
      "}\n";

#define FRAGMENT_CONVERT_YUV                                        \
   "  gl_FragColor.r = y + 1.59602678 * v;\n"                    \
   "  gl_FragColor.g = y - 0.39176229 * u - 0.81296764 * v;\n"   \
   "  gl_FragColor.b = y + 2.01723214 * u;\n"                    \
   "  gl_FragColor.a = 1.0;\n"

   const char *frag_shader_y_uv =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  float y = 1.16438356 * (texture2D(texture0, v_uv).x - 0.0625);\n"
      "  float u = texture2D(texture1, v_uv).r - 0.5;\n"
      "  float v = texture2D(texture1, v_uv).g - 0.5;\n"
      FRAGMENT_CONVERT_YUV
      "}\n";

   const char *frag_shader_y_u_v =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "uniform sampler2D texture2;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  float y = 1.16438356 * (texture2D(texture0, v_uv).x - 0.0625);\n"
      "  float u = texture2D(texture1, v_uv).x - 0.5;\n"
      "  float v = texture2D(texture2, v_uv).x - 0.5;\n"
      FRAGMENT_CONVERT_YUV
      "}\n";

   const char *frag_shader_y_xuxv =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  float y = 1.16438356 * (texture2D(texture0, v_uv).x - 0.0625);\n"
      "  float u = texture2D(texture1, v_uv).g - 0.5;\n"
      "  float v = texture2D(texture1, v_uv).a - 0.5;\n"
      FRAGMENT_CONVERT_YUV
      "}\n";

   struct ctx *context;
   if (!(context = calloc(1, sizeof(struct ctx))))
      return NULL;

   const char *str;
   str = (const char*)GL_CALL(glGetString(GL_VERSION));
   wlc_log(WLC_LOG_INFO, "GL version: %s", str ? str : "(null)");
   str = (const char*)GL_CALL(glGetString(GL_VENDOR));
   wlc_log(WLC_LOG_INFO, "GL vendor: %s", str ? str : "(null)");

   /** TODO: Should be available in GLES3 */
#if 0
   GL_CALL(glGetInternalFormativ(GL_TEXTURE_2D, GL_RGBA, GL_TEXTURE_IMAGE_FORMAT, 1, &context->preferred_format));
   GL_CALL(glGetInternalFormativ(GL_TEXTURE_2D, GL_RGBA, GL_TEXTURE_IMAGE_TYPE, 1, &context->preferred_type));
   wlc_log(WLC_LOG_INFO, "Preferred texture format: %d", context->preferred_format);
   wlc_log(WLC_LOG_INFO, "Preferred texture type: %d", context->preferred_type);
#endif

   context->extensions = (const char*)GL_CALL(glGetString(GL_EXTENSIONS));

   if (!has_extension(context, "GL_OES_EGL_image_external")) {
      wlc_log(WLC_LOG_WARN, "gles2: GL_OES_EGL_image_external not available");
      frag_shader_egl = frag_shader_dummy;
   }

   const struct {
      const char *vert;
      const char *frag;
   } map[PROGRAM_LAST] = {
      { vert_shader, frag_shader_rgb }, // PROGRAM_RGB
      { vert_shader, frag_shader_rgba }, // PROGRAM_RGBA
      { vert_shader, frag_shader_egl }, // PROGRAM_EGL
      { vert_shader, frag_shader_y_uv }, // PROGRAM_Y_UV
      { vert_shader, frag_shader_y_u_v }, // PROGRAM_Y_U_V
      { vert_shader, frag_shader_y_xuxv }, // PROGRAM_Y_XUXV
      { vert_shader, frag_shader_cursor }, // PROGRAM_CURSOR
   };

   for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
      GLuint vert = create_shader(map[i].vert, GL_VERTEX_SHADER);
      GLuint frag = create_shader(map[i].frag, GL_FRAGMENT_SHADER);
      context->programs[i].obj = glCreateProgram();
      GL_CALL(glAttachShader(context->programs[i].obj, vert));
      GL_CALL(glAttachShader(context->programs[i].obj, frag));
      GL_CALL(glLinkProgram(context->programs[i].obj));
      GL_CALL(glDeleteShader(vert));
      GL_CALL(glDeleteShader(frag));

      GLint status;
      GL_CALL(glGetProgramiv(context->programs[i].obj, GL_LINK_STATUS, &status));
      if (!status) {
         GLsizei len;
         char log[1024];
         GL_CALL(glGetProgramInfoLog(context->programs[i].obj, sizeof(log), &len, log));
         wlc_log(WLC_LOG_ERROR, "Linking:\n%*s\n", len, log);
         abort();
      }

      set_program(context, i);
      GL_CALL(glBindAttribLocation(context->programs[i].obj, 0, "pos"));
      GL_CALL(glBindAttribLocation(context->programs[i].obj, 1, "uv"));

      for (int u = 0; u < UNIFORM_LAST; ++u) {
         context->programs[i].uniforms[u] = GL_CALL(glGetUniformLocation(context->programs[i].obj, uniform_names[u]));
      }

      GL_CALL(glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE0], 0));
      GL_CALL(glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE1], 1));
      GL_CALL(glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE2], 2));
   }

   struct {
      GLenum format;
      GLuint w, h;
      GLenum type;
      const void *data;
   } images[TEXTURE_LAST] = {
      { GL_LUMINANCE, 1, 1, GL_UNSIGNED_BYTE, NULL }, // TEXTURE_BLACK
      { GL_LUMINANCE, 14, 14, GL_UNSIGNED_BYTE, cursor_palette }, // TEXTURE_CURSOR
      { GL_RGBA, 0, 0, GL_UNSIGNED_BYTE, NULL }, // TEXTURE_FAKEFB
   };

   GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
   GL_CALL(glGenTextures(TEXTURE_LAST, context->textures));

   for (GLuint i = 0; i < TEXTURE_LAST; ++i) {
      GL_CALL(glBindTexture(GL_TEXTURE_2D, context->textures[i]));
      GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
      GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
      GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, images[i].format, images[i].w, images[i].h, 0, images[i].format, images[i].type, images[i].data));
   }

   GL_CALL(glEnableVertexAttribArray(0));
   GL_CALL(glEnableVertexAttribArray(1));

   GL_CALL(glGenFramebuffers(1, &context->clear_fbo));
   GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, context->clear_fbo));
   GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, context->textures[TEXTURE_FAKEFB], 0));
   GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

   GL_CALL(glEnable(GL_BLEND));
   GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
   GL_CALL(glClearColor(0.0, 0.0, 0.0, 0.0));
   return context;
}

static void
clear_fakefb(struct ctx *context)
{
   // assumes texture already bound!
   GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, context->clear_fbo));
   GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
   GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

static void
resolution(struct ctx *context, const struct wlc_size *mode, const struct wlc_size *resolution)
{
   assert(context && resolution);

   if (!wlc_size_equals(&context->resolution, resolution)) {
      for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
         set_program(context, i);
         GL_CALL(glUniform2fv(context->program->uniforms[UNIFORM_RESOLUTION], 1, (GLfloat[]){ resolution->w, resolution->h }));
      }

      GL_CALL(glBindTexture(GL_TEXTURE_2D, context->textures[TEXTURE_FAKEFB]));
      GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, resolution->w, resolution->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
      clear_fakefb(context);
      context->resolution = *resolution;
   }

   if (!wlc_size_equals(&context->mode, mode)) {
      GL_CALL(glViewport(0, 0, mode->w, mode->h));
      context->mode = *mode;
   }
}

static void
surface_gen_textures(struct wlc_surface *surface, const GLuint num_textures)
{
   assert(surface);

   for (GLuint i = 0; i < num_textures; ++i) {
      if (surface->textures[i])
         continue;

      GL_CALL(glGenTextures(1, &surface->textures[i]));
      GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->textures[i]));
      GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
      GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
   }
}

static void
surface_flush_textures(struct wlc_surface *surface)
{
   assert(surface);

   for (GLuint i = 0; i < 3; ++i) {
      if (surface->textures[i]) {
         GL_CALL(glDeleteTextures(1, &surface->textures[i]));
      }
   }

   memset(surface->textures, 0, sizeof(surface->textures));
}

static void
surface_flush_images(struct wlc_context *context, struct wlc_surface *surface)
{
   assert(surface);

   for (GLuint i = 0; i < 3; ++i) {
      if (surface->images[i])
         wlc_context_destroy_image(context, surface->images[i]);
   }

   memset(surface->images, 0, sizeof(surface->images));
}

static void
surface_destroy(struct ctx *context, struct wlc_context *bound, struct wlc_surface *surface)
{
   (void)context;
   assert(context && bound && surface);
   surface_flush_textures(surface);
   surface_flush_images(bound, surface);
   wlc_dlog(WLC_DBG_RENDER, "-> Destroyed surface");
}

static bool
shm_attach(struct wlc_surface *surface, struct wlc_buffer *buffer, struct wl_shm_buffer *shm_buffer)
{
   assert(surface && buffer && shm_buffer);

   buffer->shm_buffer = shm_buffer;
   buffer->size.w = wl_shm_buffer_get_width(shm_buffer);
   buffer->size.h = wl_shm_buffer_get_height(shm_buffer);

   GLint pitch;
   GLenum gl_format, gl_pixel_type;
   switch (wl_shm_buffer_get_format(shm_buffer)) {
      case WL_SHM_FORMAT_XRGB8888:
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 4;
         gl_format = GL_BGRA_EXT;
         gl_pixel_type = GL_UNSIGNED_BYTE;
         surface->format = SURFACE_RGB;
         break;
      case WL_SHM_FORMAT_ARGB8888:
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 4;
         gl_format = GL_BGRA_EXT;
         gl_pixel_type = GL_UNSIGNED_BYTE;
         surface->format = SURFACE_RGBA;
         break;
      case WL_SHM_FORMAT_RGB565:
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 2;
         gl_format = GL_RGB;
         gl_pixel_type = GL_UNSIGNED_SHORT_5_6_5;
         surface->format = SURFACE_RGB;
         break;
      default:
         /* unknown shm buffer format */
         return false;
   }

   struct wlc_view *view;
   if ((view = convert_from_wlc_handle(surface->view, "view")) && view->x11.id)
      surface->format = wlc_x11_window_get_surface_format(&view->x11);

   surface_gen_textures(surface, 1);
   GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->textures[0]));
   GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch));
   GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0));
   GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0));
   wl_shm_buffer_begin_access(buffer->shm_buffer);
   void *data = wl_shm_buffer_get_data(buffer->shm_buffer);
   GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, gl_format, pitch, buffer->size.h, 0, gl_format, gl_pixel_type, data));
   wl_shm_buffer_end_access(buffer->shm_buffer);
   return true;
}

static bool
egl_attach(struct ctx *context, struct wlc_context *ectx, struct wlc_surface *surface, struct wlc_buffer *buffer, EGLint format)
{
   assert(context && surface && buffer);

   if (!context->api.glEGLImageTargetTexture2DOES) {
      if (!has_extension(context, "GL_OES_EGL_image_external") ||
          !(context->api.glEGLImageTargetTexture2DOES = wlc_context_get_proc_address(ectx, "glEGLImageTargetTexture2DOES"))) {
         wlc_log(WLC_LOG_WARN, "No GL_OES_EGL_image_external available");
         return false;
      }
      assert(context->api.glEGLImageTargetTexture2DOES);
   }

   buffer->legacy_buffer = convert_to_wl_resource(buffer, "buffer");
   wlc_context_query_buffer(ectx, buffer->legacy_buffer, EGL_WIDTH, (EGLint*)&buffer->size.w);
   wlc_context_query_buffer(ectx, buffer->legacy_buffer, EGL_HEIGHT, (EGLint*)&buffer->size.h);
   wlc_context_query_buffer(ectx, buffer->legacy_buffer, EGL_WAYLAND_Y_INVERTED_WL, (EGLint*)&buffer->y_inverted);

   GLuint num_planes;
   GLenum target = GL_TEXTURE_2D;
   switch (format) {
      case EGL_TEXTURE_RGB:
      case EGL_TEXTURE_RGBA:
      default:
         num_planes = 1;
         surface->format = SURFACE_RGBA;
         break;
      case 0x31DA:
         num_planes = 1;
         surface->format = SURFACE_EGL;
         target = GL_TEXTURE_EXTERNAL_OES;
         break;
      case EGL_TEXTURE_Y_UV_WL:
         num_planes = 2;
         surface->format = SURFACE_Y_UV;
         break;
      case EGL_TEXTURE_Y_U_V_WL:
         num_planes = 3;
         surface->format = SURFACE_Y_U_V;
         break;
      case EGL_TEXTURE_Y_XUXV_WL:
         num_planes = 2;
         surface->format = SURFACE_Y_XUXV;
         break;
   }

   struct wlc_view *view;
   if ((view = convert_from_wlc_handle(surface->view, "view")) && view->x11.id)
      surface->format = wlc_x11_window_get_surface_format(&view->x11);

   if (num_planes > 3) {
      wlc_log(WLC_LOG_WARN, "planes > 3 in egl surfaces not supported, nor should be possible");
      return false;
   }

   surface_flush_images(ectx, surface);
   surface_gen_textures(surface, num_planes);

   for (GLuint i = 0; i < num_planes; ++i) {
      EGLint attribs[] = { EGL_WAYLAND_PLANE_WL, i, EGL_NONE };
      if (!(surface->images[i] = wlc_context_create_image(ectx, EGL_WAYLAND_BUFFER_WL, buffer->legacy_buffer, attribs)))
         return false;

      GL_CALL(glActiveTexture(GL_TEXTURE0 + i));
      GL_CALL(glBindTexture(target, surface->textures[i]));
      GL_CALL(context->api.glEGLImageTargetTexture2DOES(target, surface->images[i]));
   }

   return true;
}

static bool
surface_attach(struct ctx *context, struct wlc_context *bound, struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(context && bound && surface);

   struct wl_resource *wl_buffer;
   if (!buffer || !(wl_buffer = convert_to_wl_resource(buffer, "buffer"))) {
      surface_destroy(context, bound, surface);
      return true;
   }

   EGLint format;
   bool attached = false;

   struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(wl_buffer);
   if (shm_buffer) {
      attached = shm_attach(surface, buffer, shm_buffer);
   } else if (wlc_context_query_buffer(bound, (void*)wl_buffer, EGL_TEXTURE_FORMAT, &format)) {
      attached = egl_attach(context, bound, surface, buffer, format);
   } else {
      /* unknown buffer */
      wlc_log(WLC_LOG_WARN, "Unknown buffer");
   }

   if (attached)
      wlc_dlog(WLC_DBG_RENDER, "-> Attached surface (%" PRIuWLC ") with buffer of size (%ux%u)", convert_to_wlc_resource(surface), buffer->size.w, buffer->size.h);

   return attached;
}

static void
texture_paint(struct ctx *context, GLuint *textures, GLuint nmemb, const struct wlc_geometry *geometry, struct paint *settings)
{
   const GLfloat vertices[8] = {
      geometry->origin.x + geometry->size.w, geometry->origin.y,
      geometry->origin.x, geometry->origin.y,
      geometry->origin.x + geometry->size.w, geometry->origin.y + geometry->size.h,
      geometry->origin.x, geometry->origin.y + geometry->size.h,
   };

   const GLfloat coords[8] = {
      1, 0,
      0, 0,
      1, 1,
      0, 1
   };

   set_program(context, settings->program);

   for (GLuint i = 0; i < nmemb; ++i) {
      if (!textures[i])
         break;

      GL_CALL(glActiveTexture(GL_TEXTURE0 + i));
      GL_CALL(glBindTexture(GL_TEXTURE_2D, textures[i]));

      if (settings->filter || !wlc_size_equals(&context->resolution, &context->mode)) {
         GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
         GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
      } else {
         GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
         GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
      }
   }

   GL_CALL(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices));
   GL_CALL(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, coords));
   GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
}

static void
surface_paint_internal(struct ctx *context, struct wlc_surface *surface, const struct wlc_geometry *geometry, struct paint *settings)
{
   assert(context && surface && geometry && settings);

   const struct wlc_geometry *g = geometry;

   if (!wlc_size_equals(&surface->size, &geometry->size)) {
      if (wlc_geometry_equals(&settings->visible, geometry)) {
         settings->filter = true;
      } else {
         // black borders are requested
         struct paint settings2 = *settings;
         settings2.program = (settings2.program == PROGRAM_RGBA || settings2.program == PROGRAM_RGB ? settings2.program : PROGRAM_RGB);
         texture_paint(context, &context->textures[TEXTURE_BLACK], 1, geometry, &settings2);
         g = &settings->visible;
      }
   }

   texture_paint(context, surface->textures, 3, g, settings);
}

static void
surface_paint(struct ctx *context, struct wlc_surface *surface, const struct wlc_geometry *geometry)
{
   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.program = (enum program_type)surface->format;
   settings.visible = *geometry;
   surface_paint_internal(context, surface, geometry, &settings);
}

static void
view_paint(struct ctx *context, struct wlc_view *view)
{
   assert(context && view);

   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.program = (enum program_type)surface->format;

   struct wlc_geometry geometry;
   wlc_view_get_bounds(view, &geometry, &settings.visible);
   surface_paint_internal(context, surface, &geometry, &settings);

   if (DRAW_OPAQUE) {
      wlc_view_get_opaque(view, &geometry);
      settings.visible = geometry;
      settings.program = PROGRAM_CURSOR;
      GL_CALL(glBlendFunc(GL_ONE, GL_DST_COLOR));
      texture_paint(context, &context->textures[TEXTURE_BLACK], 1, &geometry, &settings);
      GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
   }
}

static void
pointer_paint(struct ctx *context, const struct wlc_point *pos)
{
   assert(context);
   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.program = PROGRAM_CURSOR;
   struct wlc_geometry g = { *pos, { 14, 14 } };
   texture_paint(context, &context->textures[TEXTURE_CURSOR], 1, &g, &settings);
}

static void
clamp_to_bounds(struct wlc_geometry *g, const struct wlc_size *bounds)
{
   assert(g);

   // XXX: Check overflows even if unlikely

   if (g->origin.x < 0) {
      g->size.w += g->origin.x;
      g->origin.x = 0;
   } else if ((uint32_t)g->origin.x > bounds->w) {
      g->origin.x = 0;
   }

   if (g->origin.y < 0) {
      g->size.h += g->origin.y;
      g->origin.y = 0;
   } else if ((uint32_t)g->origin.y > bounds->h) {
      g->origin.y = 0;
   }

   if (g->origin.x + g->size.w > bounds->w)
      g->size.w -= (g->origin.x + g->size.w) - bounds->w;

   if (g->origin.y + g->size.h > bounds->h)
      g->size.h -= (g->origin.y + g->size.h) - bounds->h;
}

static void
read_pixels(struct ctx *context, enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data)
{
   (void)context;
   assert(context && geometry && out_geometry && out_data);
   struct wlc_geometry g = *geometry;
   clamp_to_bounds(&g, &context->resolution);
   GL_CALL(glReadPixels(g.origin.x, g.origin.y, g.size.w, g.size.h, format_map[format].format, format_map[format].type, out_data));
   *out_geometry = g;
}

static void
write_pixels(struct ctx *context, enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data)
{
   (void)context;
   assert(context && geometry && data);
   struct wlc_geometry g = *geometry;
   clamp_to_bounds(&g, &context->resolution);
   GL_CALL(glBindTexture(GL_TEXTURE_2D, context->textures[TEXTURE_FAKEFB]));
   GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, g.origin.x, g.origin.y, g.size.w, g.size.h, format_map[format].format, format_map[format].type, data));
   context->fakefb_dirty = true;
}

static void
flush_fakefb(struct ctx *context)
{
   assert(context);

   if (!context->fakefb_dirty)
      return;

   struct paint settings = {0};
   settings.program = PROGRAM_RGBA;
   texture_paint(context, &context->textures[TEXTURE_FAKEFB], 1, &(struct wlc_geometry){ .origin = { 0, 0 }, .size = context->resolution }, &settings);
   clear_fakefb(context);
   context->fakefb_dirty = false;
}

static void
clear(struct ctx *context)
{
   (void)context;
   assert(context);
   GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
}

static void
terminate(struct ctx *context)
{
   assert(context);

   for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
      GL_CALL(glDeleteProgram(context->programs[i].obj));
   }

   GL_CALL(glDeleteTextures(TEXTURE_LAST, context->textures));
   GL_CALL(glDeleteFramebuffers(1, &context->clear_fbo));
   free(context);
}

void*
wlc_gles2(struct wlc_render_api *api)
{
   assert(api);

   struct ctx *ctx;
   if (!(ctx = create_context()))
      return NULL;

   api->renderer_type = WLC_RENDERER_GLES2;
   api->terminate = terminate;
   api->resolution = resolution;
   api->surface_destroy = surface_destroy;
   api->surface_attach = surface_attach;
   api->view_paint = view_paint;
   api->surface_paint = surface_paint;
   api->pointer_paint = pointer_paint;
   api->read_pixels = read_pixels;
   api->write_pixels = write_pixels;
   api->flush_fakefb = flush_fakefb;
   api->clear = clear;

   chck_cstr_to_bool(getenv("WLC_DRAW_OPAQUE"), &DRAW_OPAQUE);

   wlc_log(WLC_LOG_INFO, "GLES2 renderer initialized");
   return ctx;
}

// 0 == black, 1 == white, 2 == transparent
static const GLubyte cursor_palette[] = {
   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
   0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
   0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
   0x01, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02,
   0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02,
   0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x00, 0x01, 0x02, 0x02, 0x02, 0x02,
   0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02
};
