#include "gles2.h"
#include "render.h"

#include "context/context.h"
#include "compositor/surface.h"
#include "compositor/buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wayland-server.h>

static struct {
   struct wlc_context *context;
   const char *extensions;

   struct {
      void *handle;
      const GLubyte* (*glGetString)(GLenum);
      void (*glEnable)(GLenum);
      void (*glClear)(GLbitfield);
      void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
      void (*glBlendFunc)(GLenum, GLenum);
      GLuint (*glCreateShader)(GLenum);
      void (*glShaderSource)(GLuint, GLsizei count, const GLchar **string, const GLint *length);
      void (*glCompileShader)(GLuint);
      void (*glGetShaderiv)(GLuint, GLenum, GLint*);
      void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
      GLuint (*glCreateProgram)(void);
      void (*glAttachShader)(GLuint, GLuint);
      void (*glLinkProgram)(GLuint);
      void (*glUseProgram)(GLuint);
      void (*glGetProgramiv)(GLuint, GLenum, GLint*);
      void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
      void (*glBindAttribLocation)(GLuint, GLuint, const GLchar*);
      void (*glEnableVertexAttribArray)(GLuint);
      void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
      void (*glDrawArrays)(GLenum, GLint, GLsizei);
      void (*glGenTextures)(GLsizei, GLuint*);
      void (*glBindTexture)(GLenum, GLuint);
      void (*glTexParameteri)(GLenum, GLenum, GLenum);
      void (*glPixelStorei)(GLenum, GLint);
      void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
   } api;
} gl;

static bool
gles2_load(void)
{
   const char *lib = "libGLESv2.so", *func = NULL;

   if (!(gl.api.handle = dlopen(lib, RTLD_LAZY)))
      return false;

#define load(x) (gl.api.x = dlsym(gl.api.handle, (func = #x)))

   if (!load(glGetString))
      goto function_pointer_exception;
   if (!load(glEnable))
      goto function_pointer_exception;
   if (!load(glClear))
      goto function_pointer_exception;
   if (!load(glClearColor))
      goto function_pointer_exception;
   if (!load(glBlendFunc))
      goto function_pointer_exception;
   if (!(load(glCreateShader)))
      goto function_pointer_exception;
   if (!(load(glShaderSource)))
      goto function_pointer_exception;
   if (!(load(glCompileShader)))
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
   if (!(load(glGetProgramiv)))
      goto function_pointer_exception;
   if (!(load(glGetProgramInfoLog)))
      goto function_pointer_exception;
   if (!(load(glEnableVertexAttribArray)))
      goto function_pointer_exception;
   if (!(load(glBindAttribLocation)))
      goto function_pointer_exception;
   if (!(load(glVertexAttribPointer)))
      goto function_pointer_exception;
   if (!(load(glDrawArrays)))
      goto function_pointer_exception;
   if (!(load(glGenTextures)))
      goto function_pointer_exception;
   if (!(load(glBindTexture)))
      goto function_pointer_exception;
   if (!(load(glTexParameteri)))
      goto function_pointer_exception;
   if (!(load(glPixelStorei)))
      goto function_pointer_exception;
   if (!(load(glTexImage2D)))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

static bool
has_extension(const char *extension)
{
   assert(extension);

   if (!gl.extensions)
      return false;

   size_t len = strlen(extension), pos;
   const char *s = gl.extensions;
   while ((pos = strcspn(s, " ")) != 0) {
      size_t next = pos + (s[pos] != 0);

      if (!strncmp(s, extension, len))
         return true;

      s += next;
   }
   return false;
}

static void
shm_attach(struct wlc_surface *surface, struct wlc_buffer *buffer, struct wl_shm_buffer *shm_buffer)
{
   buffer->shm_buffer = shm_buffer;
   buffer->width = wl_shm_buffer_get_width(shm_buffer);
   buffer->height = wl_shm_buffer_get_height(shm_buffer);

   int pitch;
   GLenum gl_format, gl_pixel_type;
   switch (wl_shm_buffer_get_format(shm_buffer)) {
      case WL_SHM_FORMAT_XRGB8888:
         // gs->shader = &gr->texture_shader_rgbx;
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 4;
         gl_format = GL_BGRA_EXT;
         gl_pixel_type = GL_UNSIGNED_BYTE;
         break;
      case WL_SHM_FORMAT_ARGB8888:
         // gs->shader = &gr->texture_shader_rgba;
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 4;
         gl_format = GL_BGRA_EXT;
         gl_pixel_type = GL_UNSIGNED_BYTE;
         break;
      case WL_SHM_FORMAT_RGB565:
         // gs->shader = &gr->texture_shader_rgbx;
         pitch = wl_shm_buffer_get_stride(shm_buffer) / 2;
         gl_format = GL_RGB;
         gl_pixel_type = GL_UNSIGNED_SHORT_5_6_5;
         break;
      default:
         /* unknown shm buffer format */
         return;
   }

   static GLuint texture = 0;
   if (!texture) {
      gl.api.glGenTextures(1, &texture);
      gl.api.glBindTexture(GL_TEXTURE_2D, texture);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   }

   gl.api.glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch);
   gl.api.glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
   gl.api.glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
   wl_shm_buffer_begin_access(buffer->shm_buffer);
   void *data = wl_shm_buffer_get_data(buffer->shm_buffer);
   gl.api.glTexImage2D(GL_TEXTURE_2D, 0, gl_format, pitch, buffer->height, 0, gl_format, gl_pixel_type, data);
   wl_shm_buffer_end_access(buffer->shm_buffer);
   wl_resource_queue_event(buffer->resource, WL_BUFFER_RELEASE);
}

static void
surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   if (!buffer) {
      /* TODO: cleanup */
      return;
   }

   struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(buffer->resource);
   if (shm_buffer) {
      shm_attach(surface, buffer, shm_buffer);
   } else if (1) {
      /* EGL buffer */
   } else {
      /* unknown buffer */
   }
}

static void
clear(void)
{
   gl.api.glClear(GL_COLOR_BUFFER_BIT);
}

static void
surface_render(struct wlc_surface *surface)
{
   const GLint vertices[8] = {
      surface->width, 0,
      0, 0,
      surface->width, surface->height,
      0, surface->height,
   };

   const GLint coords[8] = {
      1, 0,
      0, 0,
      1, 1,
      0, 1
   };

   gl.api.glVertexAttribPointer(0, 2, GL_INT, GL_FALSE, 0, vertices);
   gl.api.glVertexAttribPointer(1, 2, GL_INT, GL_FALSE, 0, coords);
   gl.api.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void
terminate(void)
{
   if (gl.api.handle)
      dlclose(gl.api.handle);

   memset(&gl, 0, sizeof(gl));
}

static GLuint
create_shader(const char *source, GLenum shader_type)
{
   GLuint shader = gl.api.glCreateShader(shader_type);
   assert(shader != 0);

   gl.api.glShaderSource(shader, 1, (const char **)&source, NULL);
   gl.api.glCompileShader(shader);

   GLint status;
   gl.api.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
   if (!status) {
      GLsizei len;
      char log[1000];
      gl.api.glGetShaderInfoLog(shader, sizeof(log), &len, log);
      fprintf(stderr, "Error: compiling %s: %*s\n",
            shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
            len, log);
      abort();
   }

   return shader;
}

bool
wlc_gles2_init(struct wlc_context *context, struct wlc_render *out_render)
{
   if (!gles2_load()) {
      terminate();
      return false;
   }

   gl.context = context;
   gl.extensions = (const char*)gl.api.glGetString(GL_EXTENSIONS);

   static const char *vert_shader_text =
      "mat4 ortho = mat4("
      "  2.0/800.0,  0,      0, 0,"
      "     0,   -2.0/480.0, 0, 0,"
      "     0,       0,     -1, 0,"
      "    -1,       1,      0, 1"
      ");\n"
      "attribute vec4 pos;\n"
      "attribute vec2 uv;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_Position = ortho * pos;\n"
      "  v_uv = uv;\n"
      "}\n";

   static const char *frag_shader_text =
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_FragColor = texture2D(texture0, v_uv);\n"
      "}\n";

   GLuint frag = create_shader(frag_shader_text, GL_FRAGMENT_SHADER);
   GLuint vert = create_shader(vert_shader_text, GL_VERTEX_SHADER);
   GLuint program = gl.api.glCreateProgram();
   gl.api.glAttachShader(program, frag);
   gl.api.glAttachShader(program, vert);
   gl.api.glLinkProgram(program);

   GLint status;
   gl.api.glGetProgramiv(program, GL_LINK_STATUS, &status);
   if (!status) {
      GLsizei len;
      char log[1000];
      gl.api.glGetProgramInfoLog(program, sizeof(log), &len, log);
      fprintf(stderr, "Error: linking:\n%*s\n", len, log);
      abort();
   }

   gl.api.glUseProgram(program);
   gl.api.glBindAttribLocation(program, 0, "pos");
   gl.api.glBindAttribLocation(program, 1, "uv");
   gl.api.glLinkProgram(program);

   gl.api.glEnableVertexAttribArray(0);
   gl.api.glEnableVertexAttribArray(1);

   gl.api.glEnable(GL_BLEND);
   gl.api.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
   gl.api.glClearColor(0.2, 0.2, 0.2, 1);

   out_render->terminate = terminate;
   out_render->api.attach = surface_attach;
   out_render->api.render = surface_render;
   out_render->api.clear = clear;
   out_render->api.swap = context->api.swap;

   fprintf(stdout, "-!- GLES2 renderer initialized\n");
   return true;
}
