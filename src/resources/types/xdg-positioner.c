#include <stdlib.h>
#include <assert.h>
#include "xdg-positioner.h"
#include "internal.h"
#include "macros.h"

static void
wlc_xdg_positioner_protocol_set_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   if (width < 1 || height < 1) {
      wl_resource_post_error(resource,
         ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
         "width and height must be positives and non-zero");
         return;
   }
   positioner->size.w = width;
   positioner->size.h = height;
   positioner->flags |= WLC_XDG_POSITIONER_HAS_SIZE;
}

static void
wlc_xdg_positioner_protocol_set_anchor_rect(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   if (width < 1 || height < 1) {
      wl_resource_post_error(resource,
         ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
         "width and height must be positives and non-zero");
         return;
   }
   positioner->anchor_rect.origin.x = x;
   positioner->anchor_rect.origin.y = y;
   positioner->anchor_rect.size.w = width;
   positioner->anchor_rect.size.h = height;
   positioner->flags |= WLC_XDG_POSITIONER_HAS_ANCHOR_RECT;
}

static void
wlc_xdg_positioner_protocol_set_anchor(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_anchor anchor)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   if (((anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP ) &&
         (anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM)) ||
         ((anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) &&
         (anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT))) {
      wl_resource_post_error(resource,
         ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
         "same-axis values are not allowed");
      return;
   }
   
   positioner->anchor = WLC_BIT_ANCHOR_NONE;
   if (anchor & ZXDG_POSITIONER_V6_ANCHOR_TOP) positioner->anchor |= WLC_BIT_ANCHOR_TOP;
   if (anchor & ZXDG_POSITIONER_V6_ANCHOR_BOTTOM) positioner->anchor |= WLC_BIT_ANCHOR_BOTTOM;
   if (anchor & ZXDG_POSITIONER_V6_ANCHOR_LEFT) positioner->anchor |= WLC_BIT_ANCHOR_LEFT;
   if (anchor & ZXDG_POSITIONER_V6_ANCHOR_RIGHT) positioner->anchor |= WLC_BIT_ANCHOR_RIGHT;
}

static void
wlc_xdg_positioner_protocol_set_gravity(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_gravity gravity)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   if (((gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) &&
         (gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM)) ||
         ((gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) &&
         (gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT))) {
      wl_resource_post_error(resource,
         ZXDG_POSITIONER_V6_ERROR_INVALID_INPUT,
         "same-axis values are not allowed");
      return;
   }
   
   positioner->gravity = WLC_BIT_GRAVITY_NONE;
   if (gravity & ZXDG_POSITIONER_V6_GRAVITY_TOP) positioner->gravity |= WLC_BIT_GRAVITY_TOP;
   if (gravity & ZXDG_POSITIONER_V6_GRAVITY_BOTTOM) positioner->gravity |= WLC_BIT_GRAVITY_BOTTOM;
   if (gravity & ZXDG_POSITIONER_V6_GRAVITY_LEFT) positioner->gravity |= WLC_BIT_GRAVITY_LEFT;
   if (gravity & ZXDG_POSITIONER_V6_GRAVITY_RIGHT) positioner->gravity |= WLC_BIT_GRAVITY_RIGHT;
}

static void
wlc_xdg_positioner_protocol_set_constraint_adjustment(struct wl_client *client, struct wl_resource *resource, enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   positioner->constraint_adjustment = WLC_BIT_CONSTRAINT_ADJUSTMENT_NONE;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_SLIDE_X) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_SLIDE_X;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_SLIDE_Y) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_FLIP_X) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_FLIP_X;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_FLIP_Y) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_FLIP_Y;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_RESIZE_X) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_RESIZE_X;
   if (constraint_adjustment & ZXDG_POSITIONER_V6_CONSTRAINT_ADJUSTMENT_RESIZE_Y) positioner->constraint_adjustment |= WLC_BIT_CONSTRAINT_ADJUSTMENT_RESIZE_Y;
}

static void
wlc_xdg_positioner_protocol_set_offset(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y)
{
   (void)client;
   struct wlc_xdg_positioner *positioner;
   if (!(positioner = wl_resource_get_user_data(resource)))
      return;
   
   positioner->offset.x = x;
   positioner->offset.y = y;
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