#ifndef _WLC_DATA_SOURCE_H_
#define _WLC_DATA_SOURCE_H_

#include <wlc/defines.h>
#include <chck/pool/pool.h>

struct wlc_data_source;
struct wlc_data_source_impl {
   void (*send)(struct wlc_data_source *data_source, const char *type, int fd);
   void (*accept)(struct wlc_data_source *data_source, const char *type);
   void (*cancel)(struct wlc_data_source *data_source);
   void (*dnd_finished)(struct wlc_data_source *data_source);
};

struct wlc_data_source {
   struct chck_iter_pool types;
   const struct wlc_data_source_impl *impl;
};

void wlc_data_source_release(struct wlc_data_source *source);
WLC_NONULL bool wlc_data_source(struct wlc_data_source *source, const struct wlc_data_source_impl *impl);

#endif /* _WLC_DATA_SOURCE_ */
