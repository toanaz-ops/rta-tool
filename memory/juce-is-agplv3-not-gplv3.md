# JUCE's open-source option is AGPLv3, not GPLv3

`JUCE/LICENSE.md` offers the framework under **AGPLv3** or a commercial licence.
It is commonly misremembered as GPLv3.

Consequences accepted for this project:

- The application is licensed **AGPL-3.0-or-later**, not GPL-3.0.
- GPLv3 code (Open Sound Meter, Friture, OpenOptimize) can still be combined in,
  because GPLv3 §13 explicitly permits linking with AGPLv3 work.
- If the project ever needs a closed-source build, a commercial JUCE licence must
  be bought. `rta_core` deliberately carries no JUCE dependency, so the
  measurement engine itself stays free of that constraint.

Source: <https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>

Related: [[asio-sdk-is-gplv3-since-oct-2025]], [[core-must-not-include-frameworks]]
