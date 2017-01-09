#include <stdlib.h>
#include <assert.h>
#include "xdg-positioner.h"
#include "internal.h"
#include "macros.h"

static void
wlc_xdg_positioner_protocol_set_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height)
{
   (void)client; (void)resource; (void)width; (void)height;
   STUBL(resource);
}

static void
wlc_xdg_positioner_protocol_set_anchor_rect(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
   STUBL(resource);
}

static void
wlc_xdg_positioner_protocol_set_anchor(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_anchor anchor)
{
   (void)client; (void)resource; (void)anchor;
   STUBL(resource);
}

static void
wlc_xdg_positioner_protocol_set_gravity(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_gravity gravity)
{
   (void)client; (void)resource; (void)gravity;
   STUBL(resource);
}

static void
wlc_xdg_positioner_protocol_set_constraint_adjustment(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment)
{
   (void)client; (void)resource; (void)constraint_adjustment;
   STUBL(resource);
}

static void
wlc_xdg_positioner_protocol_set_offset(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y)
{
	(void)client; (void)resource; (void)x; (void)y;
	STUBL(resource);
}

WLC_CONST const struct zxdg_positioner_v6_interface*
wlc_xdg_positioner_implementation(void)
{
   static const struct zxdg_positioner_v6_interface zxdg_positioner_v6_implementation = {
      .destroy = wlc_cb_resource_destructor,
      .set_size = wlc_xdg_positioner_protocol_set_size,
      .set_anchor_rect = wlc_xdg_positioner_protocol_set_anchor_rect,
      .set_anchor = wlc_xdg_positioner_protocol_set_anchor,
      .set_gravity = wlc_xdg_positioner_protocol_set_gravity,
      .set_constraint_adjustment = wlc_xdg_positioner_protocol_set_constraint_adjustment,
      .set_offset = wlc_xdg_positioner_protocol_set_offset,
   };

   return &zxdg_positioner_v6_implementation;
}