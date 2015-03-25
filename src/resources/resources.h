#ifndef _WLC_RESOURCES_H_
#define _WLC_RESOURCES_H_

#include <wlc/wlc.h>
#include <stdint.h>
#include <stdbool.h>
#include <chck/pool/pool.h>
#include <wayland-server.h>

typedef uintptr_t wlc_resource;

/** Storage for handles / resources. */
struct wlc_source {
   const char *name;
   struct chck_pool pool;
   bool (*constructor)();
   void (*destructor)();
};

/** Use this empty struct for resources that don't need their own container. */
struct wlc_resource {};

/** Generic destructor that can be passed to various wl interface implementations. */
static inline void
wlc_cb_resource_destructor(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   wl_resource_destroy(resource);
}

/** Helper for creating wayland resources with version support check. */
struct wl_resource* wl_resource_create_checked(struct wl_client *client, const struct wl_interface *interface, uint32_t version, uint32_t supported, uint32_t id);

/** Init resource management */
bool wlc_resources_init(void);

/** Terminate resource management */
void wlc_resources_terminate(void);

/**
 * Initialize source.
 * name should be type name of the handle/resource source will be carrying.
 * grow defines the reallocation step for source.
 * member defines the size of item the source will be carrying.
 */
bool wlc_source(struct wlc_source *source, const char *name, bool (*constructor)(), void (*destructor)(), size_t grow, size_t member);

/**
 * Release source and all the handles/resources it contains.
 */
void wlc_source_release(struct wlc_source *source);

/** Converts pointer back to handle, use the convert_to_<foo> macros instead. */
wlc_handle convert_to_handle(void *ptr, size_t size);

/**
 * Create new wlc_handle into given source pool.
 * wlc handles are types that are not tied to wayland resource.
 * For example wlc_view and wlc_output.
 */
void* wlc_handle_create(struct wlc_source *source);

/**
 * Convert from wlc_handle back to the pointer.
 * name should be same as the name in source, otherwise NULL is returned.
 */
void* convert_from_wlc_handle(wlc_handle handle, const char *name);

/**
 * Convert pointer back to wlc_handle.
 * NOTE: The sizeof(*x), use this only when compiler can know the size.
 * void *ptr, won't work.
 */
#define convert_to_wlc_handle(x) convert_to_handle(x, sizeof(*x))

/**
 * Release wlc_handle.
 */
void wlc_handle_release(wlc_handle handle);

/**
 * Create new wlc_resource into given source pool.
 * wlc_resources are types that are tied to wayland resource.
 * Thus their lifetime is also dictated by the wayland resource.
 *
 * Implementation for these types should go in resources/types/
 */
wlc_resource wlc_resource_create(struct wlc_source *source, struct wl_client *client, const struct wl_interface *interface, uint32_t version, uint32_t supported, uint32_t id);

/** Create new wlc_resource from existing wayland resource. */
wlc_resource wlc_resource_create_from(struct wlc_source *source, struct wl_resource *resource);

/** Implement wlc_resource. */
void wlc_resource_implement(wlc_resource resource, const void *implementation, void *userdata);

/** Convert to wlc_resource from wayland resource. */
wlc_resource wlc_resource_from_wl_resource(struct wl_resource *resource);

/** Convert to wayland resource from wlc_resource. */
struct wl_resource* wl_resource_from_wlc_resource(wlc_resource resource, const char *name);

/** Get wayland resource for client from source. */
struct wl_resource* wl_resource_for_client(struct wlc_source *source, struct wl_client *client);

/** Convert to pointer from wlc_resource. */
void* convert_from_wlc_resource(wlc_resource resource, const char *name);

/** Convert to pointer from wayland resource. */
void* convert_from_wl_resource(struct wl_resource *resource, const char *name);

/**
 * Convert to wlc_resource from pointer.
 * NOTE: The sizeof(*x), use this only when compiler can know the size.
 * void *ptr, won't work.
 */
#define convert_to_wlc_resource(x) (wlc_resource)convert_to_handle(x, sizeof(*x))

/**
 * Convert to wayland resource from pointer.
 * NOTE: The sizeof(*x), use this only when compiler can know the size.
 * void *ptr, won't work.
 */
#define convert_to_wl_resource(x, y) wl_resource_from_wlc_resource((wlc_resource)convert_to_handle(x, sizeof(*x)), y)

/**
 * Invalidate the wayland resource for the wlc_resource.
 * only wlc_buffer uses this right now, since it needs to release the wayland resource in queue.
 */
void wlc_resource_invalidate(wlc_resource resource);

/** Release resource. */
void wlc_resource_release(wlc_resource resource);

/** Release pointer to wlc_handle, useful for chck_<foo>_for_each_call mainly. */
static inline void
wlc_handle_release_ptr(wlc_handle *handle)
{
   wlc_handle_release(*handle);
}

/** Release pointer to wlc_resource, useful for chck_<foo>_for_each_call mainly. */
static inline void
wlc_resource_release_ptr(wlc_resource *resource)
{
   wlc_resource_release(*resource);
}

#endif /* _WLC_RESOURCES_H_ */
