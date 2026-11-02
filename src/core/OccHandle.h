#pragma once

#include <Standard_Handle.hxx>

namespace sc {

// Version-neutral OCCT smart pointer. OCCT 8 adds occ::handle<T>; 7.x only has
// opencascade::handle<T>. Both name the same type, so we use the latter everywhere.
template <class T>
using OccHandle = opencascade::handle<T>;

} // namespace sc
