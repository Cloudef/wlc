#include "udev.h"
#include "wlc_internal.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "seat/seat.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <libudev.h>
#include <libinput.h>
#include <wayland-server.h>

static int
udev_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_udev *udev = data;
   struct udev_device *event;

   event = udev_monitor_receive_device(udev->monitor);
   udev_device_unref(event);
   return 1;
}

static int
input_open_restricted(const char *path, int flags, void *user_data)
{
   (void)user_data;
   return wlc_fd_open(path, flags, WLC_FD_INPUT);
}

static void
input_close_restricted(int fd, void *user_data)
{
   (void)user_data;
   wlc_fd_close(fd);
}

static const struct libinput_interface libinput_implementation = {
   input_open_restricted,
   input_close_restricted,
};

static int
input_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct wlc_input *input = data;
   struct wlc_seat *seat = input->seat;

   if (libinput_dispatch(input->handle) != 0)
      puts("failed to dispatch libinput\n");

   struct libinput_event *event;
   while ((event = libinput_get_event(input->handle))) {
      struct libinput *handle = libinput_event_get_context(event);

      switch (libinput_event_get_type(event)) {
         case LIBINPUT_EVENT_DEVICE_ADDED:
            puts("INPUT DEVICE ADDED");
            break;

         case LIBINPUT_EVENT_DEVICE_REMOVED:
            puts("INPUT DEVICE REMOVED");
            break;

         case LIBINPUT_EVENT_KEYBOARD_KEY:
            {
               struct libinput_event_keyboard *kev = libinput_event_get_keyboard_event(event);
               uint32_t key = libinput_event_keyboard_get_key(kev);
               seat->notify.keyboard_key(seat, key, libinput_event_keyboard_get_key_state(kev));
            }
            break;

         case LIBINPUT_EVENT_POINTER_MOTION:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               input->pointer.x += libinput_event_pointer_get_dx(pev);
               input->pointer.y += libinput_event_pointer_get_dy(pev);
               input->pointer.x = fmax(fmin(input->pointer.x, seat->compositor->output->resolution.width), 0);
               input->pointer.y = fmax(fmin(input->pointer.y, seat->compositor->output->resolution.height), 0);
               seat->notify.pointer_motion(seat, input->pointer.x, input->pointer.y);
            }
            break;

         case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               input->pointer.x = libinput_event_pointer_get_absolute_x_transformed(pev, seat->compositor->output->resolution.width);
               input->pointer.y = libinput_event_pointer_get_absolute_y_transformed(pev, seat->compositor->output->resolution.height);
               seat->notify.pointer_motion(seat, input->pointer.x, input->pointer.y);
            }
            break;

         case LIBINPUT_EVENT_POINTER_BUTTON:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               uint32_t button = libinput_event_pointer_get_button(pev);
               seat->notify.pointer_button(seat, button, libinput_event_pointer_get_button_state(pev));
            }
            break;

         default:
            break;
      }

      libinput_event_destroy(event);
   }

   return 0;
}

static void
input_log(struct libinput *input, enum libinput_log_priority priority, const char *format, va_list args)
{
   (void)input, (void)priority;
   vfprintf(stdout, format, args);
}

static void
wlc_input_free(struct wlc_input *input)
{
   assert(input);

   if (input->handle)
      libinput_unref(input->handle);

   free(input);
}

static struct wlc_input*
wlc_input_new(struct wlc_udev *udev, struct wlc_compositor *compositor)
{
   assert(udev);

   struct wlc_input *input;
   if (!(input = calloc(1, sizeof(struct wlc_input))))
      goto fail;

   input->seat = compositor->seat;

   if (!(input->handle = libinput_udev_create_context(&libinput_implementation, input, udev->handle)))
      goto fail;

   if (!(input->event_source = wl_event_loop_add_fd(compositor->event_loop, libinput_get_fd(input->handle), WL_EVENT_READABLE, input_event, input)))
      goto event_source_fail;

   wl_event_source_check(input->event_source);

   if (libinput_udev_assign_seat(input->handle, "seat0") != 0)
      goto seat_fail;

   libinput_log_set_handler(input->handle, &input_log);
   libinput_log_set_priority(input->handle, LIBINPUT_LOG_PRIORITY_ERROR);
   return input;

event_source_fail:
   fprintf(stderr, "-!- failed to add libinput event source\n");
   goto fail;
seat_fail:
   fprintf(stderr, "-!- failed to assign seat to libinput\n");
fail:
   if (input)
      wlc_input_free(input);
   return NULL;
}

void
wlc_udev_free(struct wlc_udev *udev)
{
   assert(udev);

   if (udev->input)
      wlc_input_free(udev->input);

   if (udev->monitor_event_source)
      wl_event_source_remove(udev->monitor_event_source);

   if (udev->handle)
      udev_unref(udev->handle);

   free(udev);
}

struct wlc_udev*
wlc_udev_new(struct wlc_compositor *compositor)
{
   struct wlc_udev *udev;
   if (!(udev = calloc(1, sizeof(struct wlc_udev))))
      goto fail;

   if (!(udev->handle = udev_new()))
      goto fail;

   if (!(udev->monitor = udev_monitor_new_from_netlink(udev->handle, "udev")))
      goto fail;

   udev_monitor_filter_add_match_subsystem_devtype(udev->monitor, "drm", NULL);

#if 0
   if (!(udev->monitor_event_source = wl_event_loop_add_fd(compositor->event_loop, udev_monitor_get_fd(udev->monitor), WL_EVENT_READABLE, udev_event, udev)))
      goto event_source_fail;

   if (udev_monitor_enable_receiving(udev->monitor) < 0)
      goto failed_to_enable_receiving;

   wl_event_source_check(udev->monitor_event_source);
#endif

   if (!(udev->input = wlc_input_new(udev, compositor)))
      goto fail;

   return udev;

event_source_fail:
   fprintf(stderr, "-!- failed to add udev event source\n");
   goto fail;
failed_to_enable_receiving:
   fprintf(stderr, "-!- failed to enable udev-monitor receiving\n");
fail:
   if (udev)
      wlc_udev_free(udev);
   return NULL;
}
