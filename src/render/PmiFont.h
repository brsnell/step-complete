#pragma once

namespace sc::render {

// The bundled PMI font (STIX Two Math, SIL OFL 1.1). The default fonts OCCT finds on a
// system lack many GD&T symbols (flatness, position, maximum material condition, ...).
//
// On first use the font is extracted from the Qt resource to a cache file (the user's
// cache directory, or a temporary directory when that is not writable) and registered
// with OCCT's font manager. Returns false, and never throws, when that fails; text
// then falls back to OCCT's default font.
bool ensurePmiFont();

// Family name to put in text aspects. Registers the font on first use; when that
// failed this is a generic family name that OCCT resolves to its default font.
const char* pmiFontName();

} // namespace sc::render
