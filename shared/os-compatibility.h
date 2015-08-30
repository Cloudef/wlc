#ifndef __wlc_os_compatibility_h__
#define __wlc_os_compatibility_h__

#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <chck/string/string.h>

#if !HAVE_MKOSTEMP
static int
set_cloexec_or_close(int fd)
{
   if (fd == -1)
      return -1;

   long flags;
   if ((flags = fcntl(fd, F_GETFD)) == -1)
      goto err;

   if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
      goto err;

   return fd;

err:
   close(fd);
   return -1;
}
#endif

static int
create_tmpfile_cloexec(char *tmpname)
{
   int fd;

#if HAVE_MKOSTEMP
   if ((fd = mkostemp(tmpname, O_CLOEXEC)) >= 0)
      unlink(tmpname);
#else
   if ((fd = mkstemp(tmpname)) >= 0) {
      fd = set_cloexec_or_close(fd);
      unlink(tmpname);
   }
#endif

   return fd;
}

static int
os_create_anonymous_file(off_t size)
{
   static const char template[] = "/wlc-shared-XXXXXX";

   const char *path = getenv("XDG_RUNTIME_DIR");
   if (chck_cstr_is_empty(path))
      return -1;

   struct chck_string name = {0};
   if (!chck_string_set_format(&name, "%s%s%s", path, (chck_cstr_ends_with(path, "/") ? "" : "/"), template))
      return -1;

   int fd = create_tmpfile_cloexec(name.data);
   chck_string_release(&name);

   if (fd < 0)
      return -1;

   int ret;
#if HAVE_POSIX_FALLOCATE
   if ((ret = posix_fallocate(fd, 0, size)) != 0) {
      close(fd);
      errno = ret;
      return -1;
   }
#else
   if ((ret = ftruncate(fd, size)) < 0) {
      close(fd);
      return -1;
   }
#endif

   return fd;
}

#endif /* __wlc_os_compatibility_h__ */
