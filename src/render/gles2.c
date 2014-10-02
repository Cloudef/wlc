#include "gles2.h"
#include "render.h"

#include "context/egl.h"
#include "context/context.h"
#include "compositor/view.h"
#include "compositor/surface.h"
#include "compositor/buffer.h"
#include "shell/xdg-surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wayland-server.h>

enum {
   UNIFORM_WIDTH,
   UNIFORM_HEIGHT,
   UNIFORM_ALPHA,
   UNIFORM_LAST,
};

static const char *uniform_names[UNIFORM_LAST] = {
   "width",
   "height",
   "alpha"
};

static struct {
   struct wlc_context *context;
   const char *extensions;

   GLint uniforms[UNIFORM_LAST];

   struct {
      void *handle;
      const GLubyte* (*glGetString)(GLenum);
      void (*glEnable)(GLenum);
      void (*glClear)(GLbitfield);
      void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
      void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
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
      GLint (*glGetUniformLocation)(GLuint, const GLchar *name);
      void (*glUniform1f)(GLint, GLfloat);
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

      PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
   } api;
} gl;

static bool
gles2_load(void)
{
   const char *lib = "libGLESv2.so", *func = NULL;

   if (!(gl.api.handle = dlopen(lib, RTLD_LAZY))) {
      fprintf(stderr, "-!- %s\n", dlerror());
      return false;
   }

#define load(x) (gl.api.x = dlsym(gl.api.handle, (func = #x)))

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
   if (!(load(glGetUniformLocation)))
      goto function_pointer_exception;
   if (!(load(glUniform1f)))
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

   // Needed for EGL hw surfaces
   load(glEGLImageTargetTexture2DOES);

#undef load

   return true;

function_pointer_exception:
   fprintf(stderr, "-!- Could not load function '%s' from '%s'\n", func, lib);
   return false;
}

#if 0
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
#endif

static void
surface_gen_textures(struct wlc_surface *surface, const int num_textures)
{
   for (int i = 0; i < num_textures; ++i) {
      if (surface->textures[i])
         continue;

      gl.api.glGenTextures(1, &surface->textures[i]);
      gl.api.glBindTexture(GL_TEXTURE_2D, surface->textures[i]);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   }
}

static void
surface_flush_textures(struct wlc_surface *surface)
{
   for (int i = 0; i < 3; ++i) {
      if (surface->textures[i])
         gl.api.glDeleteTextures(1, &surface->textures[i]);
   }

   memset(surface->textures, 0, sizeof(surface->textures));
}

static void
surface_flush_images(struct wlc_surface *surface)
{
   for (int i = 0; i < 3; ++i) {
      if (surface->images[i])
         wlc_egl_destroy_image(surface->images[i]);
   }

   memset(surface->images, 0, sizeof(surface->images));
}

static void
surface_destroy(struct wlc_surface *surface)
{
   surface_flush_textures(surface);
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

   surface_gen_textures(surface, 1);
   gl.api.glBindTexture(GL_TEXTURE_2D, surface->textures[0]);
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
egl_attach(struct wlc_surface *surface, struct wlc_buffer *buffer, uint32_t format)
{
   buffer->legacy_buffer = (struct wlc_buffer*)buffer->resource;
   wlc_egl_query_buffer(buffer->legacy_buffer, EGL_WIDTH, &buffer->width);
   wlc_egl_query_buffer(buffer->legacy_buffer, EGL_HEIGHT, &buffer->height);
   wlc_egl_query_buffer(buffer->legacy_buffer, EGL_WAYLAND_Y_INVERTED_WL, (EGLint*)&buffer->y_inverted);

   int num_planes;
   GLenum target = GL_TEXTURE_2D;
   switch (format) {
      case EGL_TEXTURE_RGB:
      case EGL_TEXTURE_RGBA:
      default:
         num_planes = 1;
         // gs->shader = &gr->texture_shader_rgba;
         break;
      case 0x31DA:
         num_planes = 1;
         // gs->target = GL_TEXTURE_EXTERNAL_OES;
         // gs->shader = &gr->texture_shader_egl_external;
         break;
      case EGL_TEXTURE_Y_UV_WL:
         num_planes = 2;
         // gs->shader = &gr->texture_shader_y_uv;
         break;
      case EGL_TEXTURE_Y_U_V_WL:
         num_planes = 3;
         // gs->shader = &gr->texture_shader_y_u_v;
         break;
      case EGL_TEXTURE_Y_XUXV_WL:
         num_planes = 2;
         // gs->shader = &gr->texture_shader_y_xuxv;
         break;
   }

   if (num_planes > 3) {
      fprintf(stderr, "planes > 3 in egl surfaces not supported, nor should be possible\n");
      return;
   }

   surface_flush_images(surface);
   surface_gen_textures(surface, num_planes);

   for (int i = 0; i < num_planes; ++i) {
      EGLint attribs[] = { EGL_WAYLAND_PLANE_WL, i, EGL_NONE };
      if (!(surface->images[i] = wlc_egl_create_image(EGL_WAYLAND_BUFFER_WL, buffer->legacy_buffer, attribs)))
         continue;

      gl.api.glActiveTexture(GL_TEXTURE0 + i);
      gl.api.glBindTexture(target, surface->textures[i]);
      gl.api.glEGLImageTargetTexture2DOES(target, surface->images[i]);
   }

   wl_resource_queue_event(buffer->resource, WL_BUFFER_RELEASE);
}

static void
surface_attach(struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   if (!buffer) {
      surface_flush_images(surface);
      surface_flush_textures(surface);
      return;
   }

   int format;
   struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(buffer->resource);
   if (shm_buffer) {
      shm_attach(surface, buffer, shm_buffer);
   } else if (gl.api.glEGLImageTargetTexture2DOES && wlc_egl_query_buffer((void*)buffer->resource, EGL_TEXTURE_FORMAT, &format)) {
      egl_attach(surface, buffer, format);
   } else {
      /* unknown buffer */
      puts("unknown buffer");
   }
}

static void
view_render(struct wlc_view *view)
{
   struct wlc_geometry b;
   wlc_view_get_bounds(view, &b);

   const GLint vertices[8] = {
      b.x + b.w, b.y,
      b.x, b.y,
      b.x + b.w, b.y + b.h,
      b.x, b.y + b.h,
   };

   // printf("%d,%d+%d,%d\n", b.w, b.h, b.x, b.y);

   const GLint coords[8] = {
      1, 0,
      0, 0,
      1, 1,
      0, 1
   };

   gl.api.glUniform1f(gl.uniforms[UNIFORM_ALPHA], (view->state & WLC_BIT_ACTIVATED ? 1.0f : 0.5f));

   for (int i = 0; i < 3; ++i) {
      if (!view->surface->textures[i])
         break;

      gl.api.glActiveTexture(GL_TEXTURE0 + i);
      gl.api.glBindTexture(GL_TEXTURE_2D, view->surface->textures[i]);
   }

   if (view->surface->width != b.w || view->surface->height != b.h) {
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   } else {
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.api.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   }

   gl.api.glVertexAttribPointer(0, 2, GL_INT, GL_FALSE, 0, vertices);
   gl.api.glVertexAttribPointer(1, 2, GL_INT, GL_FALSE, 0, coords);
   gl.api.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void
clear(void)
{
   gl.api.glClear(GL_COLOR_BUFFER_BIT);
}

static void
resolution(int32_t width, int32_t height)
{
   gl.api.glUniform1f(gl.uniforms[UNIFORM_WIDTH], width);
   gl.api.glUniform1f(gl.uniforms[UNIFORM_HEIGHT], height);
   gl.api.glViewport(0, 0, width, height);
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
      "precision mediump float;\n"
      "uniform float width;\n"
      "uniform float height;\n"
      "mat4 ortho = mat4("
      "  2.0/width,  0,       0, 0,"
      "     0,   -2.0/height, 0, 0,"
      "     0,       0,      -1, 0,"
      "    -1,       1,       0, 1"
      ");\n"
      "attribute vec4 pos;\n"
      "attribute vec2 uv;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_Position = ortho * pos;\n"
      "  v_uv = uv;\n"
      "}\n";

   // TODO: Implement different shaders for different textures
   static const char *frag_shader_text =
      "precision mediump float;\n"
      "uniform sampler2D texture0;\n"
      "uniform float alpha;\n"
      "varying vec2 v_uv;\n"
      "void main() {\n"
      "  gl_FragColor = vec4(texture2D(texture0, v_uv).rgb, 1.0) * alpha;\n"
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

   for (int i = 0; i < UNIFORM_LAST; ++i)
      gl.uniforms[i] = gl.api.glGetUniformLocation(program, uniform_names[i]);

   gl.api.glEnableVertexAttribArray(0);
   gl.api.glEnableVertexAttribArray(1);

   gl.api.glEnable(GL_BLEND);
   gl.api.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
   gl.api.glClearColor(0.2, 0.2, 0.2, 1);

   out_render->terminate = terminate;
   out_render->api.destroy = surface_destroy;
   out_render->api.attach = surface_attach;
   out_render->api.render = view_render;
   out_render->api.clear = clear;
   out_render->api.swap = context->api.swap;
   out_render->api.resolution = resolution;

   fprintf(stdout, "-!- GLES2 renderer initialized\n");
   return true;
}
