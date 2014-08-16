#ifndef _WLC_CLIENT_H_
#define _WLC_CLIENT_H_

#include <wayland-util.h>

enum wlc_input_type {
   WLC_KEYBOARD,
   WLC_POINTER,
   WLC_TOUCH,
   WLC_INPUT_TYPE_LAST
};

struct wl_client;
struct wl_resource;

struct wlc_client {
   struct wl_client *wl_client;
   struct wl_resource *input[WLC_INPUT_TYPE_LAST];
   struct wl_list link;
};

struct wlc_client* wlc_client_for_client_with_wl_client_in_list(struct wl_client *wl_client, struct wl_list *list);
void wlc_client_free(struct wlc_client *client);
struct wlc_client* wlc_client_new(struct wl_client *wl_client);

#endif /* _WLC_CLIENT_H_ */
