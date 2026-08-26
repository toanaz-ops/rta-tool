# SPDX-License-Identifier: AGPL-3.0-or-later
#
# az_ui -- the SODIUM RACK design system as a JUCE module.
#
# Packaged as a module rather than a plain library so any other JUCE project
# adopts it with one line -- `juce_add_module(<path>/az_ui)` -- which is the
# same mechanism melatonin_blur uses in the handsfree project.
#
# PORTABILITY RULE: nothing in here may mention audio measurement. Palette,
# metrics, typography, drawing primitives and LookAndFeel only. The moment a
# colour is called `coherence` this stops being a design system and becomes
# this app's GUI in a different folder.

juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/az_ui)
