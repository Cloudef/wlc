#include <wayland-util.h>
#include <chck/math/math.h>
#include <chck/string/string.h>
#include "visibility.h"
#include "internal.h"
#include "resources.h"

struct handle {
   wlc_resource public;
   wlc_resource private;
   struct wlc_source *source;
};

/**
 * ↑ extends above, only used in set_userdata, get_userdata functions.
 * we extend, since resources don't need userdata. */
struct handle_public {
   struct handle handle;
   void *userdata;
};

struct resource {
   struct {
      struct wl_listener destructor;
      struct wl_resource *r;
   } wl;

   struct handle handle;
};

struct handle_info {
   void *container, *data;
   wlc_resource public, private;
};

struct chck_pool resources;
struct chck_pool handles;

static void
relocate_handle(struct handle *handle, void *dest, const void *start, const void *end)
{
   assert(handle && dest && start && end);

   if ((void*)handle->source < start || (void*)handle->source > end)
      return;

   handle->source = dest + ((void*)handle->source - start);
}

static void
relocate_handles(struct chck_pool *pool, void *dest, const void *start, const void *end)
{
   // worst case scenario
   // when source pool address changes on resize, we need to make sure all inner sources
   // are relocated to avoid access to garbage location.

   assert(dest && end);

   if (!start)
      return;

   wlc_dlog(WLC_DBG_HANDLE, "Relocating %s at range %p-%p to %p", (pool == &handles ? "handles" : "resources"), start, end, dest);

   if (pool == &resources) {
      struct resource *r;
      chck_pool_for_each(pool, r)
         relocate_handle(&r->handle, dest, start, end);
   } else {
      chck_pool_for_each_call(pool, relocate_handle, dest, start, end);
   }
}

static void
relocate_resources(struct chck_pool *pool)
{
   // worst case scenario
   // when resrouce container pool address changes, we need to reset all destructors again

   struct resource *r;
   chck_pool_for_each(pool, r) {
      if (!r->wl.r)
         continue;

      wl_list_remove(&r->wl.destructor.link);
      wl_resource_add_destroy_listener(r->wl.r, &r->wl.destructor);
   }
}

static bool
handle_create(struct chck_pool *pool, struct wlc_source *source, struct handle_info *out_info)
{
   assert(pool && source && out_info);

   size_t i;
   void *c;
   void *original = pool->items.buffer;
   if (!(c = chck_pool_add(pool, NULL, &i)))
      return false;

   if (pool == &resources && original != pool->items.buffer)
      relocate_resources(pool);

   void *v;
   size_t h;
   original = source->pool.items.buffer;
   if (!(v = chck_pool_add(&source->pool, NULL, &h)))
      goto error0;

   if (original != source->pool.items.buffer)
      relocate_handles(pool, source->pool.items.buffer, original, original + source->pool.items.allocated);

   if (i >= (wlc_resource)~0 || h >= (wlc_resource)~0)
      goto error1;

   out_info->container = c;
   out_info->data = v;
   out_info->public = i + 1;
   out_info->private = h + 1;
   memcpy(v + source->pool.items.member - sizeof(wlc_handle), &out_info->public, sizeof(wlc_handle));

   if (source->constructor && !source->constructor(v))
      goto error1;

   wlc_dlog(WLC_DBG_HANDLE, "New %s (%s) %zu", (pool == &handles ? "handle" : "resource"), source->name, i + 1);
   return true;

error1:
   chck_pool_remove(&source->pool, h);
error0:
   chck_pool_remove(pool, i);
   return false;
}

static void
handle_release(struct chck_pool *pool, struct handle *handle, void (*preremove)())
{
   assert(pool);

   if (!handle)
      return;

   if (handle->private) {
      void *v;
      if (handle->source->destructor && (v = chck_pool_get(&handle->source->pool, handle->private - 1)))
         handle->source->destructor(v);

      void *original = handle->source->pool.items.buffer;
      chck_pool_remove(&handle->source->pool, handle->private - 1);

      if (original != handle->source->pool.items.buffer)
         relocate_handles(pool, handle->source->pool.items.buffer, original, original + handle->source->pool.items.allocated);
   }

   // called right after removal of the container
   // used by resource handles to do final destruction of wayland resource
   if (preremove)
      preremove(chck_pool_get(pool, handle->public - 1));

   wlc_dlog(WLC_DBG_HANDLE, "Released %s (%s) %zu", (pool == &handles ? "handle" : "resource"), handle->source->name, handle->public);
   chck_pool_remove(pool, handle->public - 1);
}

static bool
handle_is(struct handle *handle, const char *name)
{
   return (handle ? chck_cstreq(handle->source->name, name) : false);
}

void*
handle_get(struct handle *handle, const char *name)
{
   if (!handle || !handle->private)
      return NULL;

   if (!handle_is(handle, name)) {
      wlc_log(WLC_LOG_WARN, "Tried to retrieve handle of wrong type (%s != %s)", handle->source->name, name);
      return NULL;
   }

   return chck_pool_get(&handle->source->pool, handle->private - 1);
}

wlc_handle
convert_to_handle(void *ptr, size_t size)
{
   if (!ptr)
      return 0;

   return *(wlc_handle*)(ptr + size);
}

static void
resource_invalidate(struct resource *resource)
{
   if (!resource)
      return;

   if (resource->wl.r) {
      wl_list_remove(&resource->wl.destructor.link);
      resource->wl.r = NULL;
   }
}

static void
resource_prerelase(struct resource *resource)
{
   // finally destroy the wayland resource
   // the r may be NULL here, if container called wlc_resource_invalidate
   // wlc_buffer's do this since they need to destroy the resource differently.

   struct wl_resource *r = resource->wl.r;
   resource_invalidate(resource);

   if (r)
      wl_resource_destroy(r);
}

static void
resource_release(struct resource *resource)
{
   if (!resource)
      return;

   handle_release(&resources, &resource->handle, resource_prerelase);
}

bool
wlc_resources_init(void)
{
   return (chck_pool(&resources, 32, 0, sizeof(struct resource)) && chck_pool(&handles, 32, 0, sizeof(struct handle_public)));
}

void
wlc_resources_terminate(void)
{
   chck_pool_for_each_call(&resources, wlc_resource_release_ptr);
   chck_pool_for_each_call(&handles, wlc_handle_release_ptr);
   chck_pool_release(&resources);
   chck_pool_release(&handles);
}

bool
wlc_source(struct wlc_source *source, const char *name, bool (*constructor)(), void (*destructor)(), size_t grow, size_t member)
{
   assert(source && name && grow);
   memset(source, 0, sizeof(struct wlc_source));

   source->name = name;
   source->constructor = constructor;
   source->destructor = destructor;
   return chck_pool(&source->pool, grow, 0, member + sizeof(wlc_handle));
}

void
wlc_source_release(struct wlc_source *source)
{
   if (!source)
      return;

   struct handle *h;
   chck_pool_for_each(&handles, h) {
      if (h->source != source)
         continue;

      handle_release(&handles, h, NULL);
   }

   struct resource *r;
   chck_pool_for_each(&resources, r) {
      if (r->handle.source != source)
         continue;

      resource_release(r);
   }

   chck_pool_release(&source->pool);
}

void*
wlc_handle_create(struct wlc_source *source)
{
   assert(source);

   struct handle_info info;
   if (!handle_create(&handles, source, &info))
      return NULL;

   struct handle *h = info.container;
   h->source = source;
   h->public = info.public;
   h->private = info.private;
   return info.data;
}

void*
convert_from_wlc_handle(wlc_handle handle, const char *name)
{
   if (!handle)
      return NULL;

   return handle_get(chck_pool_get(&handles, handle - 1), name);
}

void
wlc_handle_release(wlc_handle handle)
{
   if (!handle)
      return;

   handle_release(&handles, chck_pool_get(&handles, handle - 1), NULL);
}

struct wl_resource*
wl_resource_create_checked(struct wl_client *client, const struct wl_interface *interface, uint32_t version, uint32_t supported, uint32_t id)
{
   assert(client && interface);

   if (version > supported) {
      wlc_log(WLC_LOG_WARN, "Unsupported resource version (%u > %u)", version, supported);
      wl_client_post_no_memory(client);
      return NULL;
   }

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, interface, version, id))) {
      wlc_log(WLC_LOG_WARN, "Failed create resource or bad version (%u > %u)", version, supported);
      wl_client_post_no_memory(client);
      return NULL;
   }

   return resource;
}

wlc_resource
wlc_resource_create(struct wlc_source *source, struct wl_client *client, const struct wl_interface *interface, uint32_t version, uint32_t supported, uint32_t id)
{
   assert(source && client && interface);

   if (version > supported) {
      // duplicate version check, since we have more information here (name of resource)
      wlc_log(WLC_LOG_WARN, "Unsupported resource (%s) version (%u > %u)", source->name, version, supported);
      wl_client_post_no_memory(client);
      return 0;
   }

   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, interface, version, supported, id)))
      goto error0;

   wlc_resource r;
   if (!(r = wlc_resource_create_from(source, resource)))
      goto error1;

   return r;

error1:
   wl_resource_destroy(resource);
error0:
   wl_client_post_no_memory(client);
   return 0;
}

static void
wl_destructor(struct wl_listener *listener, void *data)
{
   (void)data;
   assert(listener);

   struct resource *r;
   assert((r = wl_container_of(listener, r, wl.destructor)));
   wl_list_remove(&r->wl.destructor.link);
   r->wl.r = NULL;

   wlc_dlog(WLC_DBG_HANDLE, "Destruct resource (%s) %zu", r->handle.source->name, r->handle.public);
   wlc_resource_release(r->handle.public);
}

wlc_resource
wlc_resource_create_from(struct wlc_source *source, struct wl_resource *resource)
{
   assert(source);

   if (!resource)
      return 0;

   struct handle_info info;
   if (!handle_create(&resources, source, &info))
      return 0;

   struct resource *r = info.container;
   r->handle.source = source;
   r->handle.public = info.public;
   r->handle.private = info.private;
   r->wl.r = resource;
   r->wl.destructor.notify = wl_destructor;
   wl_resource_add_destroy_listener(resource, &r->wl.destructor);
   return r->handle.public;
}

void*
convert_from_wlc_resource(wlc_resource resource, const char *name)
{
   if (!resource)
      return NULL;

   struct resource *r = chck_pool_get(&resources, resource - 1);
   return (r ? handle_get(&r->handle, name) : NULL);
}

wlc_resource
wlc_resource_from_wl_resource(struct wl_resource *resource)
{
   if (!resource)
      return 0;

   struct wl_listener *listener;
   if (!(listener = wl_resource_get_destroy_listener(resource, wl_destructor)))
      return 0;

   struct resource *r;
   if (!(r = wl_container_of(listener, r, wl.destructor)))
      return 0;

   return r->handle.public;
}

void*
convert_from_wl_resource(struct wl_resource *resource, const char *name)
{
   return convert_from_wlc_resource(wlc_resource_from_wl_resource(resource), name);
}

struct wl_resource*
wl_resource_from_wlc_resource(wlc_resource resource, const char *name)
{
   struct resource *r;
   if (!resource || !(r = chck_pool_get(&resources, resource - 1)))
      return NULL;

   if (!handle_is(&r->handle, name)) {
      wlc_log(WLC_LOG_WARN, "Tried to retrieve resource of wrong type (%s != %s)", r->handle.source->name, name);
      return NULL;
   }

   return r->wl.r;
}

struct wl_resource*
wl_resource_for_client(struct wlc_source *source, struct wl_client *client)
{
   assert(source && client);

   struct resource *r;
   chck_pool_for_each(&resources, r) {
      if (r->handle.source != source || wl_resource_get_client(r->wl.r) != client)
         continue;

      return r->wl.r;
   }

   return NULL;
}

void
wlc_resource_invalidate(wlc_resource resource)
{
   if (!resource)
      return;

   resource_invalidate(chck_pool_get(&resources, resource - 1));
}

void
wlc_resource_release(wlc_resource resource)
{
   if (!resource)
      return;

   resource_release(chck_pool_get(&resources, resource - 1));
}

void
wlc_resource_implement(wlc_resource resource, const void *implementation, void *userdata)
{
   struct resource *r;
   if (!resource || !(r = chck_pool_get(&resources, resource - 1)))
      return;

   wl_resource_set_implementation(r->wl.r, implementation, userdata, NULL);
}

WLC_API
void wlc_handle_set_user_data(wlc_handle handle, const void *userdata)
{
   struct handle_public *h;
   if (!handle || !(h = chck_pool_get(&handles, handle - 1)))
      return;

   h->userdata = (void*)userdata;
}

WLC_API void*
wlc_handle_get_user_data(wlc_handle handle)
{
   const struct handle_public *h;
   if (!handle || !(h = chck_pool_get(&handles, handle - 1)))
      return NULL;

   return h->userdata;
}
