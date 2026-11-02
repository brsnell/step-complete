#pragma once

// Container aliases and helpers that compile against both OCCT 7.6+ and 8.0+.
//
// OCCT 8 removed the per-typedef headers (TDF_LabelSequence.hxx,
// TColStd_*.hxx, AIS_ListOfInteractive.hxx, ...) in favor of spelling
// NCollection templates directly. Under OCCT 7.x some of those typedefs use
// explicit hasher arguments, so the template spelling is not the same type
// there. Pick whichever the installed OCCT provides.

#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_Label.hxx>

#include <string>

#if __has_include(<TDF_LabelSequence.hxx>)
#include <TDF_LabelSequence.hxx>
#else
#include <NCollection_Sequence.hxx>
#endif

#if __has_include(<TColStd_SequenceOfAsciiString.hxx>)
#include <TColStd_SequenceOfAsciiString.hxx>
#else
#include <NCollection_Sequence.hxx>
#endif

#if __has_include(<TColStd_HSequenceOfExtendedString.hxx>)
#include <TColStd_HSequenceOfExtendedString.hxx>
#else
#include <NCollection_HSequence.hxx>
#endif

#if __has_include(<TColStd_IndexedDataMapOfStringString.hxx>)
#include <TColStd_IndexedDataMapOfStringString.hxx>
#else
#include <NCollection_IndexedDataMap.hxx>
#endif

namespace sc::occ {

#if __has_include(<TDF_LabelSequence.hxx>)
using LabelSequence = TDF_LabelSequence;
#else
using LabelSequence = NCollection_Sequence<TDF_Label>;
#endif

#if __has_include(<TColStd_SequenceOfAsciiString.hxx>)
using AsciiStringSequence = TColStd_SequenceOfAsciiString;
#else
using AsciiStringSequence = NCollection_Sequence<TCollection_AsciiString>;
#endif

#if __has_include(<TColStd_HSequenceOfExtendedString.hxx>)
using HExtendedStringSequence = TColStd_HSequenceOfExtendedString;
#else
using HExtendedStringSequence = NCollection_HSequence<TCollection_ExtendedString>;
#endif

#if __has_include(<TColStd_IndexedDataMapOfStringString.hxx>)
using StringStringMap = TColStd_IndexedDataMapOfStringString;
#else
using StringStringMap = NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>;
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
