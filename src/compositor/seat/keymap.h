#ifndef _WLC_KEYMAP_H_
#define _WLC_KEYMAP_H_

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct xkb_keymap;
struct xkb_rule_names;
enum xkb_keymap_compile_flags;

enum wlc_modifier {
   WLC_MOD_SHIFT,
   WLC_MOD_CAPS,
   WLC_MOD_CTRL,
   WLC_MOD_ALT,
   WLC_MOD_MOD2,
   WLC_MOD_MOD3,
   WLC_MOD_LOGO,
   WLC_MOD_MOD5,
   WLC_MOD_LAST
};

enum wlc_led {
   WLC_LED_NUM,
   WLC_LED_CAPS,
   WLC_LED_SCROLL,
   WLC_LED_LAST
};

const char* WLC_MOD_NAMES[WLC_MOD_LAST];
const char* WLC_LED_NAMES[WLC_LED_LAST];

struct wlc_keymap {
   struct xkb_keymap *keymap;
   char *area;
   uint32_t format;
   uint32_t size;
   int32_t fd;
   xkb_mod_index_t mods[WLC_MOD_LAST];
   xkb_led_index_t leds[WLC_LED_LAST];
};

uint32_t wlc_keymap_get_mod_mask(struct wlc_keymap *keymap, uint32_t in);
uint32_t wlc_keymap_get_led_mask(struct wlc_keymap *keymap, struct xkb_state *xkb);
void wlc_keymap_release(struct wlc_keymap *keymap);
bool wlc_keymap(struct wlc_keymap *keymap, const struct xkb_rule_names *names, enum xkb_keymap_compile_flags flags);

#endif /* _WLC_KEYMAP_H_ */
