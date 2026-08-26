// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace az::ui
{

//==============================================================================
// Metrics. Integers because layout arithmetic is int.

inline constexpr int   spacing          = 4;   // spacing grid unit
inline constexpr int   gap              = 8;   // gap between cells (2 x spacing)
inline constexpr int   touchTarget      = 44;  // minimum tappable size in px

// The transport switches. Sized like the latching switches they imitate:
// 168 x 62 is ~2.7x the area of a 44 px square target, which is what "hit it
// without looking, in the dark, while doing something else" actually costs.
inline constexpr int   buttonCellWidth  = 168;
inline constexpr int   buttonCellHeight = 62;
inline constexpr int   clearCellWidth   = 148;   // fits a long destructive-action legend untruncated
inline constexpr int   railWidth        = 96;  // vertical-orientation cell width

inline constexpr int   mastheadHeight   = 38;
inline constexpr int   transportHeight  = 86;  // switch height + breathing room
inline constexpr int   lampHeight       = 3;   // the lit bar on an active switch
inline constexpr int   fieldHeight      = 26;  // combo / recessed field
inline constexpr int   captionHeight    = 30;  // section caption band

// THE LEGEND GUTTER. Every row in a legend column -- across every panel that
// shares the column -- starts its fields at this x. One alignment line down
// the whole stack is the difference between "a designed column" and "three
// panels that happen to be stacked", and it only works if every panel in the
// column reads the figure from here.
inline constexpr int   gutterWidth      = 74;

// The secondary legend that sits INLINE before a row's second field, where
// the value alone would be ambiguous ("8 samples" needs BUFFER; a device name
// does not need DEVICE).
inline constexpr int   inlineLegendWidth = 58;

// Both rows of a two-field grid split at the same fraction, so the four
// columns line up vertically. Rows that split differently are exactly what
// made an earlier panel look ragged.
inline constexpr float fieldSplit       = 0.42f;

// Two radii, not one. A field is a machined slot and stays tight at 3; a
// switch is a moulded cap and reads softer at 4. Using one figure for both
// makes the switches look like oversized text boxes.
// Tracking, per element, transcribed from the original design study. One
// global figure was wrong: a section caption is set wider than a column
// heading, and a switch legend tighter than either.
inline constexpr float trackingCaption  = 0.20f;  // section captions, brand
inline constexpr float trackingColumn   = 0.16f;  // column headings, small legends
inline constexpr float trackingSwitch   = 0.15f;  // transport switch legends

// Type sizes, same source. Named rather than typed at each call site so the
// scale can be checked against the spec table in one place.
// The study's figures were measured against a browser rendering Saira
// Condensed with its own hinting. Set at 11/10.5 in the app the same face came
// out noticeably smaller, so both are up one step. The RATIO between them --
// caption above column heading -- is what the study actually fixes.
// Raised again after a second look at the running app. Saira Condensed is a
// CONDENSED face: at the study's browser figures it renders both smaller and
// lighter than the study did, and the small legends came out reading as a grey
// haze rather than as words. The RATIOS are what the study fixes; the absolute
// sizes have to suit the face actually being drawn.
inline constexpr float captionFontSize  = 14.5f;   // section caption
inline constexpr float columnFontSize   = 13.0f;   // column heading

// A 168 x 62 switch carried a 16 px legend, which left it looking like a large
// button with small writing on it. The legend is the whole point of the cell.
inline constexpr float switchFontSize   = 21.0f;   // transport switch legend
inline constexpr float hintFontSize     = 11.0f;   // switch second line (mono)
inline constexpr float brandFontSize    = 16.0f;   // brand wordmark
inline constexpr float segmentFontSize  = 12.0f;   // toolbar segment (mono, sentence case)
inline constexpr float tableFontSize    = 12.5f;   // data table cell (mono)
inline constexpr float readoutFontSize  = 12.0f;   // header readout line (mono)
inline constexpr float countdownFontSize = 26.0f;  // countdown number (mono medium)
inline constexpr float chipFontSize     = 12.5f;   // ghost chip (mono, sentence case)
inline constexpr float dangerFontSize   = 16.0f;   // destructive action legend

inline constexpr float cornerRadius     = 3.0f;   // fields, wells, chips
inline constexpr float switchRadius     = 4.0f;   // transport switches
inline constexpr float baseFontSize     = 14.0f;
inline constexpr float legendFontSize   = 12.0f;

} // namespace az::ui
