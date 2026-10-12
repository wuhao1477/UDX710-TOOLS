/* Compatibility shims for building with a newer host glibc against the
 * device's glibc 2.27 runtime.  The cross linker otherwise selects newer
 * stat ABI entry points even though the host headers expose newer defaults. */

#include <pthread.h>
#include <sys/stat.h>

typedef int (*TargetMainFunction)(int, char **, char **);
typedef void (*TargetInitFunction)(void);

extern int __xstat(int version, const char *path, struct stat *buf);
extern int __lxstat(int version, const char *path, struct stat *buf);

/*
 * Newer cross libc linkers select the GLIBC_2.34 pthread entry points by
 * default.  The device ships glibc 2.27, where these symbols are provided by
 * the GLIBC_2.17 ABI.  Keep the public calls local to this executable and
 * forward them to the older symbol versions that are available on the
 * device.
 */
extern int pthread_create_glibc_217(
    pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *arg);
extern int pthread_join_glibc_217(pthread_t thread, void **retval);
extern int pthread_detach_glibc_217(pthread_t thread);
extern int libc_start_main_glibc_217(
    TargetMainFunction main_function, int argc, char **argv,
    TargetInitFunction init, TargetInitFunction fini,
    TargetInitFunction rtld_fini, void *stack_end);

__asm__(".symver pthread_create_glibc_217,pthread_create@GLIBC_2.17");
__asm__(".symver pthread_join_glibc_217,pthread_join@GLIBC_2.17");
__asm__(".symver pthread_detach_glibc_217,pthread_detach@GLIBC_2.17");
__asm__(".symver libc_start_main_glibc_217,__libc_start_main@GLIBC_2.17");

int stat(const char *path, struct stat *buf) {
  return __xstat(0, path, buf);
}

int lstat(const char *path, struct stat *buf) {
  return __lxstat(0, path, buf);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
  return pthread_create_glibc_217(thread, attr, start_routine, arg);
}

int pthread_join(pthread_t thread, void **retval) {
  return pthread_join_glibc_217(thread, retval);
}

int pthread_detach(pthread_t thread) {
  return pthread_detach_glibc_217(thread);
}

int __libc_start_main(TargetMainFunction main_function, int argc, char **argv,
                      TargetInitFunction init, TargetInitFunction fini,
                      TargetInitFunction rtld_fini, void *stack_end) {
  return libc_start_main_glibc_217(main_function, argc, argv, init, fini,
                                   rtld_fini, stack_end);
}
