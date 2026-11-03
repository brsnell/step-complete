#pragma once

namespace sc::core {

// Hands freed heap memory back to the operating system where the allocator keeps
// it otherwise (glibc keeps the STEP reader's hundreds of MB mapped after an
// import). A no-op on other platforms. Cheap enough to call after each import.
void releaseFreedMemory();

} // namespace sc::core
