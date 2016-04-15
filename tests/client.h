#ifndef __wlc_client_h__
#define __wlc_client_h__

#include "os-compatibility.h"
#include <wayland-client.h>
#include <chck/lut/lut.h>
#include <chck/pool/pool.h>
#include <sys/signal.h>
#include <wlc/wlc.h>

#undef NDEBUG
#include <assert.h>

static int TEST_EXIT_STATUS = EXIT_FAILURE;

struct compositor_test {
   const char *name;
   pid_t client;
};

struct client_test {
   const char *name;
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_shell *shell;
   struct wl_shm *shm;
   struct wl_seat *seat;

#ifdef BACKGROUND_CLIENT_PROTOCOL_H
   struct background *background;
#endif

   struct {
      struct wl_surface *surface;
      struct wl_shell_surface *ssurface;
      size_t width, height;
   } view;

   struct {
      struct wl_buffer *wbuf;
      void *data;
   } buffer;

   struct {
      struct wl_keyboard *keyboard;
      struct wl_touch *touch;
      struct wl_pointer *pointer;
   } input;

   struct chck_hash_table formats;
   struct chck_iter_pool outputs;
};

struct output {
   struct chck_iter_pool modes;
   struct wl_output *output;
   const char *make, *model;
   int32_t width, height, subpixel, transform, scale;
   bool done;
};

struct mode {
   uint32_t flags;
   int32_t width, height, refresh;
};

static inline void
create_shm_buffer(struct client_test *test)
{
   int fd;
   const size_t stride = test->view.width * 4;
   const size_t size = stride * test->view.height;
   assert((fd = os_create_anonymous_file(size)) >= 0);
   assert((test->buffer.data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) != MAP_FAILED);
   struct wl_shm_pool *pool;
   assert((pool = wl_shm_create_pool(test->shm, fd, size)));
   assert((test->buffer.wbuf = wl_shm_pool_create_buffer(pool, 0, test->view.width, test->view.height, stride, WL_SHM_FORMAT_ARGB8888)));
   wl_shm_pool_destroy(pool);
   close(fd);
}

static inline void
shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
   assert(shm);
   struct client_test *test;
   assert((test = data));
   chck_hash_table_set(&test->formats, format, (bool[]){ true });
}

struct wl_shm_listener shm_listener = {
   .format = shm_format
};

static inline void
seat_handle_capabilities(void *data, struct wl_seat *seat, enum wl_seat_capability caps)
{
   struct client_test *test;
   assert((test = data) && seat);
   assert((caps & WL_SEAT_CAPABILITY_TOUCH) && (caps & WL_SEAT_CAPABILITY_POINTER) && (caps & WL_SEAT_CAPABILITY_KEYBOARD));

   if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !test->input.keyboard) {
      assert((test->input.keyboard = wl_seat_get_keyboard(seat)));
      // wl_keyboard_add_listener(input->keyboard, &keyboard_listener, data);
   } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && test->input.keyboard) {
      wl_keyboard_destroy(test->input.keyboard);
      test->input.keyboard = NULL;
   }

   if ((caps & WL_SEAT_CAPABILITY_POINTER) && !test->input.pointer) {
      assert((test->input.pointer = wl_seat_get_pointer(seat)));
      // wl_pointer_add_listener(input->pointer, &pointer_listener, data);
   } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && test->input.pointer) {
      wl_pointer_destroy(test->input.pointer);
      test->input.pointer = NULL;
   }

   if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !test->input.touch) {
      assert((test->input.touch = wl_seat_get_touch(seat)));
      // wl_touch_add_listener(input->touch, &touch_listener, data);
   } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && test->input.touch) {
      wl_touch_destroy(test->input.touch);
      test->input.touch = NULL;
   }
}

static const struct wl_seat_listener seat_listener = {
  .capabilities =  seat_handle_capabilities,
};

static inline void
add_seat(struct client_test *test, uint32_t name, uint32_t version)
{
   (void)version;
   assert(test);
   assert((test->seat = wl_registry_bind(test->registry, name, &wl_seat_interface, 1)));
   wl_seat_add_listener(test->seat, &seat_listener, test);
}

static inline void
handle_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
   (void)data;
   wl_shell_surface_pong(shell_surface, serial);
}

static inline void
handle_configure(void *data, struct wl_shell_surface *shell_surface, uint32_t edges, int32_t width, int32_t height)
{
   (void)data, (void)shell_surface, (void)edges, (void)width, (void)height;
}

static inline void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
   (void)data, (void)shell_surface;
}

static const struct wl_shell_surface_listener shell_surface_listener = {
   .ping = handle_ping,
   .configure = handle_configure,
   .popup_done = handle_popup_done
};

static inline struct output*
output_for_wl_output(struct client_test *test, struct wl_output *wl_output)
{
   struct output *o;
   chck_iter_pool_for_each(&test->outputs, o) {
      if (o->output == wl_output)
         return o;
   }

   return NULL;
}

static inline void
handle_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform)
{
   struct client_test *test;
   assert((test = data) && wl_output);
   (void)x, (void)y, (void)physical_width, (void)physical_height, (void)subpixel, (void)make, (void)model, (void)transform;

   struct output *o;
   if (!(o = output_for_wl_output(test, wl_output))) {
      assert((o = chck_iter_pool_push_back(&test->outputs, NULL)));
      assert(chck_iter_pool(&o->modes, 0, 4, sizeof(struct mode)));
   }

   chck_iter_pool_flush(&o->modes);
   o->output = wl_output;
   o->width = physical_width;
   o->height = physical_height;
   o->subpixel = subpixel;
   o->make = make;
   o->model = model;
   o->transform = transform;
   o->done = false;
}

static inline void
handle_done(void *data, struct wl_output *wl_output)
{
   struct client_test *test;
   assert((test = data) && wl_output);

   struct output *o;
   assert((o = output_for_wl_output(test, wl_output)));
   assert(!o->done);
   o->done = true;
}

static inline void
handle_scale(void *data, struct wl_output *wl_output, int32_t scale)
{
   struct client_test *test;
   assert((test = data) && wl_output);

   struct output *o;
   assert((o = output_for_wl_output(test, wl_output)));
   assert(!o->done);
   o->scale = scale;
}

static inline void
handle_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
   (void)wl_output, (void)refresh, (void)height;

   struct client_test *test;
   assert((test = data) && wl_output);

   struct output *o;
   assert((o = output_for_wl_output(test, wl_output)));

   struct mode mode = {
      .flags = flags,
      .width = width,
      .height = height,
      .refresh = refresh
   };

   assert(chck_iter_pool_push_back(&o->modes, &mode));
}

static const struct wl_output_listener output_listener = {
   .geometry = handle_geometry,
   .mode = handle_mode,
   .done = handle_done,
   .scale = handle_scale
};

static inline void
handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
   struct client_test *test;
   assert((test = data));

   if (chck_cstreq(interface, "wl_compositor")) {
      assert((test->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1)));
   } else if (chck_cstreq(interface, "wl_shell")) {
      assert((test->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1)));
   } else if (chck_cstreq(interface, "wl_shm")) {
      assert((test->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1)));
      wl_shm_add_listener(test->shm, &shm_listener, test);
   } else if (chck_cstreq(interface, "wl_seat")) {
      add_seat(test, name, version);
   } else if (chck_cstreq(interface, "wl_output")) {
      struct wl_output *o;
      assert((o = wl_registry_bind(registry, name, &wl_output_interface, 2)));
      wl_output_add_listener(o, &output_listener, test);
#ifdef BACKGROUND_CLIENT_PROTOCOL_H
   } else if (chck_cstreq(interface, "background")) {
      assert((test->background = wl_registry_bind(registry, name, &background_interface, 1)));
#endif
   }
}

static inline void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
   (void)data, (void)registry, (void)name;
}

static const struct wl_registry_listener registry_listener = {
   .global = handle_global,
   .global_remove = handle_global_remove,
};

static inline void
client_test_roundtrip(struct client_test *test)
{
   wl_display_dispatch(test->display);
   wl_display_roundtrip(test->display);
}

static inline void
setup_signals(void (*sigterm)(int signal))
{
   {
      struct sigaction action = {
         .sa_handler = SIG_DFL,
         .sa_flags = SA_NOCLDWAIT
      };

      sigaction(SIGCHLD, &action, NULL);
   }

   {
      struct sigaction action = {
         .sa_handler = sigterm,
      };

      sigaction(SIGUSR2, &action, NULL);
      sigaction(SIGTERM, &action, NULL);
      sigaction(SIGINT, &action, NULL);
   }
}

static inline void
client_sigterm(int signal)
{
   TEST_EXIT_STATUS = (signal == SIGUSR2 ? EXIT_SUCCESS : EXIT_FAILURE);
   kill(getppid(), (signal == SIGUSR2 ? SIGUSR2 : SIGINT));
}

static inline void
client_test_create(struct client_test *test, const char *name, uint32_t width, uint32_t height)
{
   assert(test && name);
   memset(test, 0, sizeof(struct client_test));
   test->name = name;
   test->view.width = width;
   test->view.height = height;
   setup_signals(client_sigterm);
   assert(chck_hash_table(&test->formats, 0, 128, sizeof(bool)));
   assert(chck_iter_pool(&test->outputs, 0, 4, sizeof(struct output)));
   assert((test->display = wl_display_connect(NULL)));
   assert((test->registry = wl_display_get_registry(test->display)));
   wl_registry_add_listener(test->registry, &registry_listener, test);
   client_test_roundtrip(test);
   assert(*(bool*)chck_hash_table_get(&test->formats, WL_SHM_FORMAT_ARGB8888));
}

static inline int
client_test_end(struct client_test *test)
{
   (void)test;
   kill(getppid(), SIGUSR2);
   return TEST_EXIT_STATUS;
}

static inline void
surface_create(struct client_test *test)
{
   assert(test);
   assert((test->view.surface = wl_compositor_create_surface(test->compositor)));
   create_shm_buffer(test);
   memset(test->buffer.data, 64, test->view.width * test->view.height * 4);
   wl_surface_attach(test->view.surface, test->buffer.wbuf, 0, 0);
   wl_surface_damage(test->view.surface, 0, 0, test->view.width, test->view.height);
   wl_surface_commit(test->view.surface);
}

static inline void
shell_surface_create(struct client_test *test)
{
   assert(test);
   assert((test->view.ssurface = wl_shell_get_shell_surface(test->shell, test->view.surface)));
   wl_shell_surface_add_listener(test->view.ssurface, &shell_surface_listener, test);
   wl_shell_surface_set_toplevel(test->view.ssurface);
}

static inline void
compositor_sigterm(int signal)
{
   TEST_EXIT_STATUS = (signal == SIGUSR2 ? EXIT_SUCCESS : EXIT_FAILURE);
   wlc_terminate();
}

static inline void
cb_log(enum wlc_log_type type, const char *str)
{
   (void)type;
   printf("%s\n", str);
}

static inline void
compositor_test_create(struct compositor_test *test, const char *name)
{
   assert(test && name);
   memset(test, 0, sizeof(struct compositor_test));
   test->name = name;
   wlc_log_set_handler(cb_log);
   setup_signals(compositor_sigterm);
   assert(wlc_init());
}

static inline void
compositor_test_fork_client(struct compositor_test *test, int (*main)(void))
{
   test->client = fork();
   assert(test->client >= 0);

   if (test->client != 0)
      return;

   _exit(main());
}

static inline int
compositor_test_end(struct compositor_test *test)
{
   kill(test->client, SIGINT);
   return TEST_EXIT_STATUS;
}

static inline bool
signal_client(const struct compositor_test *test)
{
   assert(kill(test->client, SIGUSR2) != -1);
   return true;
}

#endif /* __wlc_client_h__ */
