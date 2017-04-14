#ifndef _WLC_RENDER_H_
#define _WLC_RENDER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <wlc/defines.h>
#include <wlc/geometry.h>
#include <wlc/wlc-wayland.h>

/**
 * The functions in this file provide some basic rendering capabilities.
 * *_render(), *_read(), *_write() functions should only be called during post/pre render callbacks.
 * wlc_output_schedule_render() is exception and may be used to force wlc to render new frame (causing callbacks to trigger).
 *
 * For more advanced drawing you should directly use GLES2.
 * This is not documented as it's currently relying on the implementation details of wlc.
 */

/** Allowed pixel formats. */
enum wlc_pixel_format {
   WLC_RGBA8888
};

/**
 * Write pixel data with the specific format to output's framebuffer.
 * If the geometry is out of bounds, it will be automaticall clamped.
 */
WLC_NONULL void wlc_pixels_write(enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data);

/**
 * Read pixel data from output's framebuffer.
 * If the geometry is out of bounds, it will be automatically clamped.
 * Potentially clamped geometry will be stored in out_geometry, to indicate width / height of the returned data.
 */
WLC_NONULL void wlc_pixels_read(enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data);

/** Renders surface. */
WLC_NONULL void wlc_surface_render(wlc_resource surface, const struct wlc_geometry *geometry);

/**
 * Schedules output for rendering next frame. If output was already scheduled this is no-op,
 * if output is currently rendering, it will render immediately after.
 */
void wlc_output_schedule_render(wlc_handle output);

/**
 * Adds frame callbacks of the given surface for the next output frame.
 * It applies recursively to all subsurfaces.
 * Useful when the compositor creates custom animations which require disabling internal rendering,
 * but still need to update the surface textures (for ex. video players).
 */
void wlc_surface_flush_frame_callbacks(wlc_resource surface);

/** Enabled renderers */
enum wlc_renderer {
    WLC_RENDERER_GLES2,
    WLC_NO_RENDERER
};

/** Returns currently active renderer on the given output */
enum wlc_renderer wlc_output_get_renderer(wlc_handle output);

enum wlc_surface_format {
    SURFACE_RGB,
    SURFACE_RGBA,
    SURFACE_EGL,
    SURFACE_Y_UV,
    SURFACE_Y_U_V,
    SURFACE_Y_XUXV,
	 INVALID,
};

/**
 * Fills out_textures[] with the textures of a surface. Returns false if surface is invalid.
 * Array must have at least 3 elements and should be refreshed at each frame.
 * Note that these are not only OpenGL textures but rather render-specific.
 * For more info what they are check the renderer's source code */
bool wlc_surface_get_textures(wlc_resource surface, uint32_t out_textures[3], enum wlc_surface_format *out_format);

/**
 * Fills out_textures[] with the texture(s) of a current pointer specified for output.
 * Returns false, if there is no current texture, however, out_textures can still contain default pointer
 * provided by wlc. If there was an error, it will return false and out_texture[1] will be 0 and out_format 
 * will be INVALID. Tip will contain the pixel offset of the actual tip inside the texture,
 * size will contain the texture size information. */
bool wlc_get_active_pointer(wlc_handle output, uint32_t out_textures[3], enum wlc_surface_format *out_format, struct wlc_point *tip, struct wlc_size *size);

#ifdef __cplusplus
}
#endif

#endif /* _WLC_RENDER_H_ */
