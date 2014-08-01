#ifndef _WLC_CONTEXT_H_
#define _WLC_CONTEXT_H_

struct wlc_seat;
struct wlc_backend;

struct wlc_context {
   void (*terminate)(void);

   struct {
      void (*swap)(void);
   } api;
};

void wlc_context_terminate(struct wlc_context *context);
struct wlc_context* wlc_context_init(struct wlc_backend *backend);

#endif /* _WLC_CONTEXT_H_ */
