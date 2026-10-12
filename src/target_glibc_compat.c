/* Compatibility shims for building with a newer host glibc against the
 * device's glibc 2.27 runtime.  The cross linker otherwise selects newer
 * stat ABI entry points even though the host headers expose newer defaults. */

#include <sys/stat.h>

extern int __xstat(int version, const char *path, struct stat *buf);
extern int __lxstat(int version, const char *path, struct stat *buf);

int stat(const char *path, struct stat *buf) {
  return __xstat(0, path, buf);
}

int lstat(const char *path, struct stat *buf) {
  return __lxstat(0, path, buf);
}
