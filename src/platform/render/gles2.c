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

static GLfloat DIM = 0.5f;

static const GLubyte cursor_palette[];

enum program_type {
   PROGRAM_RGB,
   PROGRAM_RGBA,
   PROGRAM_EGL,
   PROGRAM_Y_UV,
   PROGRAM_Y_U_V,
   PROGRAM_Y_XUXV,
   PROGRAM_CURSOR,
   PROGRAM_BG,
   PROGRAM_LAST,
};

enum {
   UNIFORM_TEXTURE0,
   UNIFORM_TEXTURE1,
   UNIFORM_TEXTURE2,
   UNIFORM_RESOLUTION,
   UNIFORM_TIME,
   UNIFORM_DIM,
   UNIFORM_LAST,
};

enum {
   TEXTURE_BLACK,
   TEXTURE_CURSOR,
   TEXTURE_LAST
};

static const char *uniform_names[UNIFORM_LAST] = {
   "texture0",
   "texture1",
   "texture2",
   "resolution",
   "time",
   "dim",
};

struct ctx {
   const char *extensions;

   struct ctx_program *program;

   struct ctx_program {
      GLuint obj;
      GLuint uniforms[UNIFORM_LAST];
      GLuint frames;
   } programs[PROGRAM_LAST];

   struct wlc_size resolution;

   GLuint time;

   GLuint textures[TEXTURE_LAST];

   struct {
      // EGL surfaces
      PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   } api;
};

struct paint {
   struct wlc_geometry visible;
   GLfloat dim;
   enum program_type program;
   bool filter;
};

static struct {
   struct {
      void *handle;

      GLenum (*glGetError)(void);
      const GLubyte* (*glGetString)(GLenum);
      void (*glEnable)(GLenum);
      void (*glClear)(GLbitfield);
      void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
      void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
      void (*glBlendFunc)(GLenum, GLenum);
      GLuint (*glCreateShader)(GLenum);
      void (*glShaderSource)(GLuint, GLsizei count, const GLchar **string, const GLint *length);
      void (*glCompileShader)(GLuint);
      void (*glDeleteShader)(GLuint);
      void (*glGetShaderiv)(GLuint, GLenum, GLint*);
      void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
      GLuint (*glCreateProgram)(void);
      void (*glAttachShader)(GLuint, GLuint);
      void (*glLinkProgram)(GLuint);
      void (*glUseProgram)(GLuint);
      void (*glDeleteProgram)(GLuint);
      void (*glGetProgramiv)(GLuint, GLenum, GLint*);
      void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
      void (*glBindAttribLocation)(GLuint, GLuint, const GLchar*);
      GLint (*glGetUniformLocation)(GLuint, const GLchar *name);
      void (*glUniform1i)(GLint, GLint);
      void (*glUniform1fv)(GLint, GLsizei count, GLfloat*);
      void (*glUniform2fv)(GLint, GLsizei count, GLfloat*);
      void (*glEnableVertexAttribArray)(GLuint);
      void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
      void (*glDrawArrays)(GLenum, GLint, GLsizei);
      void (*glGenTextures)(GLsizei, GLuint*);
      void (*glDeleteTextures)(GLsizei, GLuint*);
      void (*glBindTexture)(GLenum, GLuint);
      void (*glActiveTexture)(GLenum);
      void (*glTexParameteri)(GLenum, GLenum, GLenum);
      void (*glPixelStorei)(GLenum, GLint);
      void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
      void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);

      PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   } api;
} gl;

static bool
gles2_load(void)
{
   const char *lib = "libGLESv2.so", *func = NULL;

   if (!(gl.api.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (gl.api.x = dlsym(gl.api.handle, (func = #x)))

   if (!(load(glGetError)))
      goto function_pointer_exception;
   if (!load(glGetString))
      goto function_pointer_exception;
   if (!load(glEnable))
      goto function_pointer_exception;
   if (!load(glClear))
      goto function_pointer_exception;
   if (!load(glClearColor))
      goto function_pointer_exception;
   if (!load(glViewport))
      goto function_pointer_exception;
   if (!load(glBlendFunc))
      goto function_pointer_exception;
   if (!(load(glCreateShader)))
      goto function_pointer_exception;
   if (!(load(glShaderSource)))
      goto function_pointer_exception;
   if (!(load(glCompileShader)))
      goto function_pointer_exception;
   if (!(load(glDeleteShader)))
      goto function_pointer_exception;
   if (!(load(glGetShaderiv)))
      goto function_pointer_exception;
   if (!(load(glGetShaderInfoLog)))
      goto function_pointer_exception;
   if (!(load(glCreateProgram)))
      goto function_pointer_exception;
   if (!(load(glAttachShader)))
      goto function_pointer_exception;
   if (!(load(glLinkProgram)))
      goto function_pointer_exception;
   if (!(load(glUseProgram)))
      goto function_pointer_exception;
   if (!(load(glDeleteProgram)))
      goto function_pointer_exception;
   if (!(load(glGetProgramiv)))
      goto function_pointer_exception;
   if (!(load(glGetProgramInfoLog)))
      goto function_pointer_exception;
   if (!(load(glEnableVertexAttribArray)))
      goto function_pointer_exception;
   if (!(load(glBindAttribLocation)))
      goto function_pointer_exception;
   if (!(load(glGetUniformLocation)))
      goto function_pointer_exception;
   if (!(load(glUniform1i)))
      goto function_pointer_exception;
   if (!(load(glUniform1fv)))
      goto function_pointer_exception;
   if (!(load(glUniform2fv)))
      goto function_pointer_exception;
   if (!(load(glVertexAttribPointer)))
      goto function_pointer_exception;
   if (!(load(glDrawArrays)))
      goto function_pointer_exception;
   if (!(load(glGenTextures)))
      goto function_pointer_exception;
   if (!(load(glDeleteTextures)))
      goto function_pointer_exception;
   if (!(load(glBindTexture)))
      goto function_pointer_exception;
   if (!(load(glActiveTexture)))
      goto function_pointer_exception;
   if (!(load(glTexParameteri)))
      goto function_pointer_exception;
   if (!(load(glPixelStorei)))
      goto function_pointer_exception;
   if (!(load(glTexImage2D)))
      goto function_pointer_exception;
   if (!(load(glReadPixels)))
      goto function_pointer_exception;

   // Needed for EGL hw surfaces
   load(glEGLImageTargetTexture2DOES);

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

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
void gl_call(const char *func, uint32_t line, const char *glfunc)
{
   GLenum error;
   if ((error = gl.api.glGetError()) == GL_NO_ERROR)
      return;

   wlc_log(WLC_LOG_ERROR, "gles2: function %s at line %u: %s == %s", func, line, glfunc, gl_error_string(error));
}

#ifndef __STRING
#  define __STRING(x) #x
#endif

#define GL_CALL(x) x; gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

static bool
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

   if (&context->programs[type] == context->program)
      return;

   context->program = &context->programs[type];
   GL_CALL(gl.api.glUseProgram(context->program->obj));
}

static GLuint
create_shader(const char *source, GLenum shader_type)
{
   assert(source);

   GLuint shader = gl.api.glCreateShader(shader_type);
   assert(shader != 0);

   GL_CALL(gl.api.glShaderSource(shader, 1, (const char **)&source, NULL));
   GL_CALL(gl.api.glCompileShader(shader));

   GLint status;
   GL_CALL(gl.api.glGetShaderiv(shader, GL_COMPILE_STATUS, &status));
   if (!status) {
      GLsizei len;
      char log[1024];
      GL_CALL(gl.api.glGetShaderInfoLog(shader, sizeof(log), &len, log));
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
      "mat4 ortho = mat4("
      "  2.0/resolution.x,         0,          0, 0,"
      "          0,        -2.0/resolution.y,  0, 0,"
      "          0,                0,         -1, 0,"
      "         -1,                1,          0, 1"
      ");\n"
      "attribute vec4 pos;\n"
      "attribute vec2 uv;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
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

   const char *frag_shader_bg =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform float time;\n"
      "uniform vec2 resolution;\n"
      "varying vec2 v_uv;\n"
      "#define M_PI 3.1415926535897932384626433832795\n"
      "float impulse(float x, float k) {\n"
      "  float h = k * x;\n"
      "  return h * exp(1.0 - h);\n"
      "}\n"
      "void main() {\n"
      "  vec3 color = vec3(0.0);\n"
      "  vec2 pos = (v_uv * 4.0 - 2.0);\n"
      "  float frame = time * M_PI * 10.0;\n"
      "  float f = impulse(0.01, sin(frame) + 1.0) + 0.25;\n"
      "  color += vec3(0.15, 0.3, 0.35) * (1.0 / distance(vec2(1.0, 0.0), pos) * f);\n"
      "  for (int i = 0; i < 3; ++i) {\n"
      "     float t = frame + (float(i) * 1.8);\n"
      "     color += vec3(0.15, 0.18, 0.15) * float(i + 1) * (1.0 / distance(vec2(sin(t * 0.8) * 0.5 + 1.0, cos(t) * 0.5), pos) * 0.09);\n"
      "  }\n"
      "  gl_FragColor = vec4(color, 1.0);\n"
      "}\n";

   const char *frag_shader_rgb =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform float dim;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(texture2D(texture0, v_uv).rgb * dim, 1.0);\n"
      "}\n";

   const char *frag_shader_rgba =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform float dim;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  vec4 col = texture2D(texture0, v_uv);\n"
      "  gl_FragColor = vec4(col.rgb * dim, col.a);\n"
      "}\n";

   const char *frag_shader_egl =
      "#version 100\n"
      "#extension GL_OES_EGL_image_external : require\n"
      "precision mediump float;\n"
      "uniform samplerExternalOES texture0;\n"
      "uniform float dim;\n"
      "varying vec2 v_uv;\n"
      "void main()\n"
      "{\n"
      "  vec4 col = texture2D(texture0, v_uv);\n"
      "  gl_FragColor = vec4(col.rgb * dim, col.a)\n;"
      "}\n";

#define FRAGMENT_CONVERT_YUV                                        \
      "  y *= dim;\n"                                               \
      "  u *= dim;\n"                                               \
      "  v *= dim;\n"                                               \
      "  gl_FragColor.r = y + 1.59602678 * v;\n"                    \
      "  gl_FragColor.g = y - 0.39176229 * u - 0.81296764 * v;\n"   \
      "  gl_FragColor.b = y + 2.01723214 * u;\n"                    \
      "  gl_FragColor.a = 1.0;\n"

   const char *frag_shader_y_uv =
      "#version 100\n"
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "uniform float dim;\n"
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
      "uniform float dim;\n"
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
      "uniform float dim;\n"
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

   context->extensions = (const char*)GL_CALL(gl.api.glGetString(GL_EXTENSIONS));

   if (has_extension(context, "GL_OES_EGL_image_external")) {
      context->api.glEGLImageTargetTexture2DOES = gl.api.glEGLImageTargetTexture2DOES;
   } else {
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
      { vert_shader, frag_shader_bg }, // PROGRAM_BG
   };

   for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
      GLuint vert = create_shader(map[i].vert, GL_VERTEX_SHADER);
      GLuint frag = create_shader(map[i].frag, GL_FRAGMENT_SHADER);
      context->programs[i].obj = gl.api.glCreateProgram();
      GL_CALL(gl.api.glAttachShader(context->programs[i].obj, vert));
      GL_CALL(gl.api.glAttachShader(context->programs[i].obj, frag));
      GL_CALL(gl.api.glLinkProgram(context->programs[i].obj));
      GL_CALL(gl.api.glDeleteShader(vert));
      GL_CALL(gl.api.glDeleteShader(frag));

      GLint status;
      GL_CALL(gl.api.glGetProgramiv(context->programs[i].obj, GL_LINK_STATUS, &status));
      if (!status) {
         GLsizei len;
         char log[1024];
         GL_CALL(gl.api.glGetProgramInfoLog(context->programs[i].obj, sizeof(log), &len, log));
         wlc_log(WLC_LOG_ERROR, "Linking:\n%*s\n", len, log);
         abort();
      }

      set_program(context, i);
      GL_CALL(gl.api.glBindAttribLocation(context->programs[i].obj, 0, "pos"));
      GL_CALL(gl.api.glBindAttribLocation(context->programs[i].obj, 1, "uv"));

      for (int u = 0; u < UNIFORM_LAST; ++u) {
         context->programs[i].uniforms[u] = GL_CALL(gl.api.glGetUniformLocation(context->programs[i].obj, uniform_names[u]));
      }

      GL_CALL(gl.api.glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE0], 0));
      GL_CALL(gl.api.glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE1], 1));
      GL_CALL(gl.api.glUniform1i(context->programs[i].uniforms[UNIFORM_TEXTURE2], 2));
   }

   struct {
      GLenum format;
      GLuint w, h;
      GLenum type;
      const void *data;
   } images[TEXTURE_LAST] = {
      { GL_LUMINANCE, 1, 1, GL_UNSIGNED_BYTE, (GLubyte[]){ 0 } }, // TEXTURE_BLACK
      { GL_LUMINANCE, 14, 14, GL_UNSIGNED_BYTE, cursor_palette }, // TEXTURE_CURSOR
   };

   GL_CALL(gl.api.glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
   GL_CALL(gl.api.glGenTextures(TEXTURE_LAST, context->textures));

   for (GLuint i = 0; i < TEXTURE_LAST; ++i) {
      GL_CALL(gl.api.glBindTexture(GL_TEXTURE_2D, context->textures[i]));
      GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
      GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
      GL_CALL(gl.api.glTexImage2D(GL_TEXTURE_2D, 0, images[i].format, images[i].w, images[i].h, 0, images[i].format, images[i].type, images[i].data));
   }

   GL_CALL(gl.api.glEnableVertexAttribArray(0));
   GL_CALL(gl.api.glEnableVertexAttribArray(1));

   GL_CALL(gl.api.glEnable(GL_BLEND));
   GL_CALL(gl.api.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
   GL_CALL(gl.api.glClearColor(0.0, 0.0, 0.0, 1));

   context->programs[PROGRAM_BG].frames = 4096 * 2;
   return context;
}

static void
resolution(struct ctx *context, const struct wlc_size *resolution)
{
   assert(context && resolution);

   if (!wlc_size_equals(&context->resolution, resolution)) {
      for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
         set_program(context, i);
         GL_CALL(gl.api.glUniform2fv(context->program->uniforms[UNIFORM_RESOLUTION], 1, (GLfloat[]){ resolution->w, resolution->h }));
      }

      GL_CALL(gl.api.glViewport(0, 0, resolution->w, resolution->h));
      context->resolution = *resolution;
   }
}

static void
surface_gen_textures(struct wlc_surface *surface, const GLuint num_textures)
{
   assert(surface);

   for (GLuint i = 0; i < num_textures; ++i) {
      if (surface->textures[i])
         continue;

      GL_CALL(gl.api.glGenTextures(1, &surface->textures[i]));
      GL_CALL(gl.api.glBindTexture(GL_TEXTURE_2D, surface->textures[i]));
      GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
      GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
   }
}

static void
surface_flush_textures(struct wlc_surface *surface)
{
   assert(surface);

   for (GLuint i = 0; i < 3; ++i) {
      if (surface->textures[i]) {
         GL_CALL(gl.api.glDeleteTextures(1, &surface->textures[i]));
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
   GL_CALL(gl.api.glBindTexture(GL_TEXTURE_2D, surface->textures[0]));
   GL_CALL(gl.api.glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch));
   GL_CALL(gl.api.glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0));
   GL_CALL(gl.api.glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0));
   wl_shm_buffer_begin_access(buffer->shm_buffer);
   void *data = wl_shm_buffer_get_data(buffer->shm_buffer);
   GL_CALL(gl.api.glTexImage2D(GL_TEXTURE_2D, 0, gl_format, pitch, buffer->size.h, 0, gl_format, gl_pixel_type, data));
   wl_shm_buffer_end_access(buffer->shm_buffer);
   return true;
}

static bool
egl_attach(struct ctx *context, struct wlc_context *ectx, struct wlc_surface *surface, struct wlc_buffer *buffer, EGLint format)
{
   assert(context && surface && buffer);

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

      GL_CALL(gl.api.glActiveTexture(GL_TEXTURE0 + i));
      GL_CALL(gl.api.glBindTexture(target, surface->textures[i]));
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
   } else if (context->api.glEGLImageTargetTexture2DOES && wlc_context_query_buffer(bound, (void*)wl_buffer, EGL_TEXTURE_FORMAT, &format)) {
      attached = egl_attach(context, bound, surface, buffer, format);
   } else {
      /* unknown buffer */
      wlc_log(WLC_LOG_WARN, "Unknown buffer");
   }

   if (attached)
      wlc_dlog(WLC_DBG_RENDER, "-> Attached surface (%zu) with buffer of size (%ux%u)", convert_to_wlc_resource(surface), buffer->size.w, buffer->size.h);

   return attached;
}

static void
texture_paint(struct ctx *context, GLuint *textures, GLuint nmemb, struct wlc_geometry *geometry, struct paint *settings)
{
   const GLfloat vertices[8] = {
      geometry->origin.x + geometry->size.w, geometry->origin.y,
      geometry->origin.x,                    geometry->origin.y,
      geometry->origin.x + geometry->size.w, geometry->origin.y + geometry->size.h,
      geometry->origin.x,                    geometry->origin.y + geometry->size.h,
   };

   const GLfloat coords[8] = {
      1, 0,
      0, 0,
      1, 1,
      0, 1
   };

   set_program(context, settings->program);

   if (settings->dim > 0.0f) {
      GL_CALL(gl.api.glUniform1fv(context->program->uniforms[UNIFORM_DIM], 1, &settings->dim));
   }

   if (context->program->frames > 0) {
      const GLfloat frame = ((context->time / 16) % context->program->frames);
      GLfloat time = frame / context->program->frames;
      GL_CALL(gl.api.glUniform1fv(context->program->uniforms[UNIFORM_TIME], 1, &time));
   }

   for (GLuint i = 0; i < nmemb; ++i) {
      if (!textures[i])
         break;

      GL_CALL(gl.api.glActiveTexture(GL_TEXTURE0 + i));
      GL_CALL(gl.api.glBindTexture(GL_TEXTURE_2D, textures[i]));

      if (settings->filter) {
         GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
         GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
      } else {
         GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
         GL_CALL(gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
      }
   }

   GL_CALL(gl.api.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices));
   GL_CALL(gl.api.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, coords));
   GL_CALL(gl.api.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
}

static void
surface_paint_internal(struct ctx *context, struct wlc_surface *surface, struct wlc_geometry *geometry, struct paint *settings)
{
   assert(context && surface && geometry && settings);

   if (!wlc_size_equals(&surface->size, &geometry->size)) {
      if (wlc_geometry_equals(&settings->visible, geometry)) {
         settings->filter = true;
      } else {
         // black borders are requested
         struct paint settings2 = *settings;
         settings2.program = (settings2.program == PROGRAM_RGBA || settings2.program == PROGRAM_RGB ? settings2.program : PROGRAM_RGB);
         texture_paint(context, &context->textures[TEXTURE_BLACK], 1, geometry, &settings2);
         memcpy(geometry, &settings->visible, sizeof(struct wlc_geometry));
      }
   }

   texture_paint(context, surface->textures, 3, geometry, settings);
}

static void
surface_paint(struct ctx *context, struct wlc_surface *surface, struct wlc_origin *pos)
{
   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.dim = 1.0f;
   settings.program = (enum program_type)surface->format;
   surface_paint_internal(context, surface, &(struct wlc_geometry){ { pos->x, pos->y }, { surface->size.w, surface->size.h } }, &settings);
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
   settings.dim = ((view->commit.state & WLC_BIT_ACTIVATED) || (view->type & WLC_BIT_UNMANAGED) ? 1.0f : DIM);
   settings.program = (enum program_type)surface->format;

   struct wlc_geometry geometry;
   wlc_view_get_bounds(view, &geometry, &settings.visible);
   surface_paint_internal(context, surface, &geometry, &settings);
}

static void
pointer_paint(struct ctx *context, struct wlc_origin *pos)
{
   assert(context);
   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.program = PROGRAM_CURSOR;
   struct wlc_geometry g = { *pos, { 14, 14 } };
   texture_paint(context, &context->textures[TEXTURE_CURSOR], 1, &g, &settings);
}

static void
read_pixels(struct ctx *context, struct wlc_geometry *geometry, void *out_data)
{
   (void)context;
   assert(context && geometry && out_data);
   GL_CALL(gl.api.glReadPixels(geometry->origin.x, geometry->origin.y, geometry->size.w, geometry->size.h, GL_RGBA, GL_UNSIGNED_BYTE, out_data));
}

static void
frame_time(struct ctx *context, GLuint time)
{
   assert(context);
   context->time = time;
}

static void
background(struct ctx *context)
{
   assert(context);
   struct paint settings;
   memset(&settings, 0, sizeof(settings));
   settings.program = PROGRAM_BG;
   struct wlc_geometry g = { { 0, 0 }, context->resolution };
   texture_paint(context, NULL, 0, &g, &settings);
}

static void
clear(struct ctx *context)
{
   (void)context;
   assert(context);
   GL_CALL(gl.api.glClear(GL_COLOR_BUFFER_BIT));
}

static void
terminate(struct ctx *context)
{
   assert(context);

   for (GLuint i = 0; i < PROGRAM_LAST; ++i) {
      GL_CALL(gl.api.glDeleteProgram(context->programs[i].obj));
   }

   GL_CALL(gl.api.glDeleteTextures(TEXTURE_LAST, context->textures));
   free(context);
}

static void
unload_egl(void)
{
   if (gl.api.handle)
      dlclose(gl.api.handle);

   memset(&gl, 0, sizeof(gl));
}

void*
wlc_gles2(struct wlc_render_api *api)
{
   assert(api);

   if (!gl.api.handle && !gles2_load()) {
      unload_egl();
      return NULL;
   }

   struct ctx *gl;
   if (!(gl = create_context()))
      return NULL;

   api->terminate = terminate;
   api->resolution = resolution;
   api->surface_destroy = surface_destroy;
   api->surface_attach = surface_attach;
   api->view_paint = view_paint;
   api->surface_paint = surface_paint;
   api->pointer_paint = pointer_paint;
   api->read_pixels = read_pixels;
   api->background = background;
   api->clear = clear;
   api->time = frame_time;

   const char *dimenv;
   if ((dimenv = getenv("WLC_DIM")))
      DIM = strtof(dimenv, NULL);

   wlc_log(WLC_LOG_INFO, "GLES2 renderer initialized");
   return gl;
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
