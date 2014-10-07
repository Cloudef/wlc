#ifndef _WLC_CONTEXT_H_
#define _WLC_CONTEXT_H_

#include <stdbool.h>

struct wlc_backend;
struct wlc_compositor;
struct wlc_output;

struct wlc_context {
   void (*terminate)(void);

   struct {
      bool (*bind)(struct wlc_output *output);
      bool (*attach)(struct wlc_output *output);
      void (*destroy)(struct wlc_output *output);
      void (*swap)(struct wlc_output *output);
   } api;
};

void wlc_context_terminate(struct wlc_context *context);
struct wlc_context* wlc_context_init(struct wlc_compositor *compositor, struct wlc_backend *backend);

#endif /* _WLC_CONTEXT_H_ */
