include_directories(src)
add_library(chck_wlc STATIC src/chck/pool/pool.c src/chck/lut/lut.c src/chck/string/string.c src/chck/thread/queue/queue.c)
