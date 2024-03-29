// RUN: %clangxx_tsan -O1 %s -DLIB -fPIC -fno-sanitize=thread -shared -o %T/libignore_lib1.so
// RUN: %clangxx_tsan -O1 %s -o %t
// RUN: echo running w/o suppressions:
// RUN: %deflake %run %t | FileCheck %s --check-prefix=CHECK-NOSUPP
// RUN: echo running with suppressions:
// RUN: %env_tsan_opts=suppressions='%s.supp' %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-WITHSUPP

// REQUIRES: stable-runtime

// Previously the test episodically failed with:
//   ThreadSanitizer: called_from_lib suppression '/libignore_lib1.so$' is
//   matched against 2 libraries: '/libignore_lib1.so' and '/libignore_lib1.so'
// This was caused by non-atomicity of reading of /proc/self/maps.

#ifndef LIB

#include <dlfcn.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <string>
#include "test.h"

#ifndef MAP_32BIT
# define MAP_32BIT 0
#endif

void *thr(void *arg) {
  // This thread creates lots of separate mappings in /proc/self/maps before
  // the ignored library.
  for (int i = 0; i < 10000; i++) {
    if (i == 5000)
      barrier_wait(&barrier);
    mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1 , 0);
    mmap(0, 4096, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1 , 0);
  }
  return 0;
}

int main(int argc, char **argv) {
  barrier_init(&barrier, 2);
  pthread_t th;
  pthread_create(&th, 0, thr, 0);
  barrier_wait(&barrier);
  std::string lib = std::string(dirname(argv[0])) + "/libignore_lib1.so";
  void *h = dlopen(lib.c_str(), RTLD_GLOBAL | RTLD_NOW);
  if (h == 0)
    exit(printf("failed to load the library (%d)\n", errno));
  void (*f)() = (void(*)())dlsym(h, "libfunc");
  if (f == 0)
    exit(printf("failed to find the func (%d)\n", errno));
  pthread_join(th, 0);
  f();
}

#else  // #ifdef LIB

#include "ignore_lib_lib.h"

#endif  // #ifdef LIB

// CHECK-NOSUPP: WARNING: ThreadSanitizer: data race
// CHECK-NOSUPP: OK

// CHECK-WITHSUPP-NOT: WARNING: ThreadSanitizer: data race
// CHECK-WITHSUPP: OK

