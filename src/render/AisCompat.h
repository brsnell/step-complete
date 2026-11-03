#pragma once

// AIS container aliases that compile against OCCT 7.6+ and 8.0+ (see core/OccCompat.h).

#include <Standard_Version.hxx>

#if OCC_VERSION_HEX >= 0x080000
#include <AIS_InteractiveObject.hxx>
#include <NCollection_List.hxx>
#else
#include <AIS_ListOfInteractive.hxx>
#endif

namespace sc::occ {

#if OCC_VERSION_HEX >= 0x080000
using InteractiveList = NCollection_List<opencascade::handle<AIS_InteractiveObject>>;
#else
using InteractiveList = AIS_ListOfInteractive;
#endif

} // namespace sc::occ
