#pragma once

// AIS container aliases that compile against OCCT 7.6+ and 8.0+ (see core/OccCompat.h).

#if __has_include(<AIS_ListOfInteractive.hxx>)
#include <AIS_ListOfInteractive.hxx>
#else
#include <AIS_InteractiveObject.hxx>
#include <NCollection_List.hxx>
#endif

namespace sc::occ {

#if __has_include(<AIS_ListOfInteractive.hxx>)
using InteractiveList = AIS_ListOfInteractive;
#else
using InteractiveList = NCollection_List<opencascade::handle<AIS_InteractiveObject>>;
#endif

} // namespace sc::occ
