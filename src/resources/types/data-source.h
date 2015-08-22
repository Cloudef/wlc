#ifndef _WLC_DATA_SOURCE_H_
#define _WLC_DATA_SOURCE_H_

#include <wlc/defines.h>
#include <chck/pool/pool.h>

struct wlc_data_source {
   struct chck_iter_pool types;
};

void wlc_data_source_release(struct wlc_data_source *source);
WLC_NONULL bool wlc_data_source(struct wlc_data_source *source);

#endif /* _WLC_DATA_SOURCE_ */
