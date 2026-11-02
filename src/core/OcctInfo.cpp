#include "core/OcctInfo.h"

#include <Standard_Version.hxx>

namespace sc::core {

std::string occtVersion() {
  return OCC_VERSION_COMPLETE;
}

} // namespace sc::core
