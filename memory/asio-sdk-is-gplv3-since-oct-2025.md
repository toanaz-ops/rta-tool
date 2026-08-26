# ASIO SDK became GPLv3-compatible in October 2025

Steinberg dual-licensed the ASIO SDK under **GPLv3** alongside the existing
proprietary licence in October 2025 (VST3 moved to MIT at the same time).

**Why this matters here:** for two decades the ASIO licence forbade
redistribution in open-source software. That is the reason every open-source
Windows measurement tool — Open Sound Meter included — shipped without ASIO and
lost to Smaart on latency and channel count, and why users fell back to
ASIO4ALL. That barrier is gone. This project can legally ship multichannel ASIO
under AGPL-3.0.

The SDK is still not vendored into this repo; it is fetched at configure time
behind `RTA_ENABLE_ASIO` (see `app/CMakeLists.txt`), because bundling a copy adds
nothing and complicates the licence notice.

Source: <https://cdm.link/open-steinberg-vst3-and-asio/>

Related: [[juce-is-agplv3-not-gplv3]]
