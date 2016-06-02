#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>

#ifdef ENABLE_XWAYLAND

#include <stdint.h>
#include <chck/lut/lut.h>

enum wlc_view_state_bit;

struct wlc_x11_window {
   uint32_t id; // xcb_window_t
   uint32_t surface_id;
   bool override_redirect;
   bool has_utf8_title;
   bool has_delete_window;
   bool has_alpha;
   bool hidden; // HACK: used by output.c to hide invisible windows
   bool paired; // is this window paired to wlc_view?
};

WLC_NONULL static inline bool
wlc_x11_is_valid_window(struct wlc_x11_window *w)
{
   return w->id;
}

WLC_NONULL static inline bool
wlc_x11_is_window_hidden(struct wlc_x11_window *w)
{
   return w->hidden;
}

WLC_NONULL static inline void
wlc_x11_set_window_hidden(struct wlc_x11_window *w, bool hidden)
{
   w->hidden = hidden;
}

struct wlc_xwm {
   struct wl_event_source *event_source;
   struct chck_hash_table paired, unpaired;

   struct {
      struct wl_listener surface;
   } listener;
};

WLC_NONULL void wlc_x11_window_set_surface_format(struct wlc_surface *surface, struct wlc_x11_window *win);
WLC_NONULL void wlc_x11_window_configure(struct wlc_x11_window *win, const struct wlc_geometry *g);
WLC_NONULL void wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle);
WLC_NONULL bool wlc_x11_window_set_active(struct wlc_x11_window *win, bool active);
WLC_NONULL void wlc_x11_window_close(struct wlc_x11_window *win);

WLC_NONULL bool wlc_xwm(struct wlc_xwm *xwm);
void wlc_xwm_release(struct wlc_xwm *xwm);

#else /* !ENABLE_XWAYLAND */

struct wlc_x11_window {};
struct wlc_xwm {};

WLC_NONULL static inline bool
wlc_x11_is_valid_window(struct wlc_x11_window *w)
{
   (void)w;
   return false;
}

WLC_NONULL static inline bool
wlc_x11_is_window_hidden(struct wlc_x11_window *w)
{
   (void)w;
   return false;
}

WLC_NONULL static inline void
wlc_x11_set_window_hidden(struct wlc_x11_window *w, bool hidden)
{
   (void)w;
   (void)hidden;
}

WLC_NONULL static inline void
wlc_x11_window_set_surface_format(struct wlc_surface *surface, struct wlc_x11_window *win)
{
   (void)surface;
   (void)win;
}

WLC_NONULL static inline void
wlc_x11_window_configure(struct wlc_x11_window *win, const struct wlc_geometry *g)
{
   (void)win;
   (void)g;
}

WLC_NONULL static inline void
wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle)
{
   (void)win;
   (void)state;
   (void)toggle;
}

WLC_NONULL static inline bool
wlc_x11_window_set_active(struct wlc_x11_window *win, bool active)
{
   (void)win;
   (void)active;
   return false;
}

WLC_NONULL static inline void
wlc_x11_window_close(struct wlc_x11_window *win)
{
   (void)win;
}

WLC_NONULL static inline bool
wlc_xwm(struct wlc_xwm *xwm)
{
   (void)xwm;
   return true;
}

static inline void
wlc_xwm_release(struct wlc_xwm *xwm)
{
   (void)xwm;
}

#endif /* ENABLE_XWAYLAND */

#endif /* _WLC_XWM_H_ */
