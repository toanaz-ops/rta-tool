// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Typography.h"

namespace az::ui
{

namespace
{

/// The installed faces, parsed once each.
///
/// createSystemTypefaceFor parses an entire font file, so doing it per
/// juce::Font would re-parse a 100 kB TTF on every drawText. These are built on
/// installation and then only ever read.
struct Faces
{
    juce::Typeface::Ptr legendSemiBold, legendBold, body, mono, monoMedium;
    bool installed = false;
};

Faces& faces()
{
    // Deliberately leaked. A function-local static of Typeface::Ptr is
    // destroyed during full program exit, AFTER the host's
    // ScopedJuceInitialiser_GUI has already torn JUCE down -- which corrupts
    // the heap on the way out (measured: STATUS_HEAP_CORRUPTION in every
    // short-lived CLI use of this module). These faces live for the process
    // lifetime by definition, so never running their destructor is the
    // correct shape, not a shortcut.
    static Faces* f = new Faces();
    return *f;
}

juce::Typeface::Ptr parse (const TypefaceSet::Blob& blob)
{
    if (blob.data == nullptr || blob.size <= 0)
        return nullptr;

    return juce::Typeface::createSystemTypefaceFor (blob.data, (std::size_t) blob.size);
}

/// Builds FontOptions from a face, or from JUCE's default if none was
/// installed. Every accessor below funnels through here so the fallback and the
/// assertion live in exactly one place.
juce::FontOptions options (const juce::Typeface::Ptr& face)
{
    if (face != nullptr)
        return juce::FontOptions (face);

    // Reached only when setTypefaces() was never called, or was called with an
    // empty blob for this slot.
    jassertfalse;
    return juce::FontOptions {};
}

} // namespace

void setTypefaces (const TypefaceSet& set)
{
    auto& f = faces();
    f.legendSemiBold = parse (set.legendSemiBold);
    f.legendBold     = parse (set.legendBold);
    f.body           = parse (set.body);
    f.mono           = parse (set.mono);
    f.monoMedium     = parse (set.monoMedium);
    f.installed      = true;
}

juce::Font baseFont (const float height)
{
    return juce::Font (options (faces().body).withHeight (height));
}

juce::Font monoFont (const float height, const bool medium)
{
    const auto& f = faces();
    return juce::Font (options (medium ? f.monoMedium : f.mono).withHeight (height));
}

juce::Font legendFont (const float height, const bool bold, const float tracking)
{
    const auto& f = faces();
    auto font = juce::Font (options (bold ? f.legendBold : f.legendSemiBold).withHeight (height));
    return font.withExtraKerningFactor (tracking);
}

} // namespace az::ui
