#include "core/Memory.h"

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace sc::core {

void releaseFreedMemory() {
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
}

} // namespace sc::core
