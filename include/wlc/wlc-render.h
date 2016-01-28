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
   WLC_RGBA8888,
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

#ifdef __cplusplus
}
#endif

#endif /* _WLC_RENDER_H_ */
