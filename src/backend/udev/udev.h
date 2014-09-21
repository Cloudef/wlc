#ifndef _WLC_UDEV_H_
#define _WLC_UDEV_H_

struct udev;
struct udev_monitor;
struct libinput;
struct wlc_compositor;
struct wlc_seat;

struct wlc_input {
   struct libinput *handle;
   struct wl_event_source *event_source;
   struct wlc_seat *seat;

   struct {
      double x, y;
   } pointer;
};

struct wlc_udev {
   struct udev *handle;
   struct udev_monitor *monitor;
   struct wl_event_source *monitor_event_source;
   struct wlc_input *input;
   char *seat_id;
};

void wlc_udev_free(struct wlc_udev *udev);
struct wlc_udev* wlc_udev_new(struct wlc_compositor *compositor);

#endif /* _WLC_UDEV_H_ */
