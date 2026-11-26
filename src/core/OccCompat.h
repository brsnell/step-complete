#pragma once

// Container aliases and helpers that compile against both OCCT 7.6+ and 8.0+.
//
// OCCT 8 deprecates the per-container typedefs (TDF_LabelSequence,
// TColStd_*, AIS_ListOfInteractive, ...) in favor of spelling NCollection
// templates directly. Under OCCT 7.x some of those typedefs use explicit
// hasher arguments, so the template spelling is not the same type there.
// Pick by version: the typedefs on 7.x, the templates on 8+.

#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>

#include <string>

#if OCC_VERSION_HEX >= 0x080000
#include <NCollection_HSequence.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_Map.hxx>
#include <NCollection_Sequence.hxx>
#else
#include <TColStd_HSequenceOfExtendedString.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TColStd_MapOfAsciiString.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TDF_LabelSequence.hxx>
#endif

namespace sc::occ {

#if OCC_VERSION_HEX >= 0x080000
// OCCT 8's RTTI macros (DEFINE_STANDARD_RTTI_INLINE, ...) name `occ::handle`. In a file that also
// sees this namespace, `occ::` inside namespace sc means sc::occ, so forward the name: without it
// a class declared with those macros under sc fails to compile wherever this header comes first.
using ::occ::handle;
#endif

#if OCC_VERSION_HEX >= 0x080000
using LabelSequence = NCollection_Sequence<TDF_Label>;
using AsciiStringSequence = NCollection_Sequence<TCollection_AsciiString>;
using HExtendedStringSequence = NCollection_HSequence<TCollection_ExtendedString>;
using StringStringMap = NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>;
using AsciiStringMap = NCollection_Map<TCollection_AsciiString>;
#else
using LabelSequence = TDF_LabelSequence;
using AsciiStringSequence = TColStd_SequenceOfAsciiString;
using HExtendedStringSequence = TColStd_HSequenceOfExtendedString;
using StringStringMap = TColStd_IndexedDataMapOfStringString;
using AsciiStringMap = TColStd_MapOfAsciiString;
#endif

// Message of an OCCT exception (GetMessageString() is deprecated in OCCT 8).
inline std::string errorMessage(const Standard_Failure& failure) {
#if OCC_VERSION_HEX >= 0x080000
  const char* text = failure.what();
#else
  const char* text = failure.GetMessageString();
#endif
  return text != nullptr ? std::string(text) : std::string("unknown error");
}

} // namespace sc::occ
