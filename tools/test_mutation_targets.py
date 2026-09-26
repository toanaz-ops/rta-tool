# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/mutation_targets.py -- mapping a mutated path to its
ctest target."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutation_targets  # noqa: E402


def test_core_path_maps_to_core_tests():
    assert mutation_targets.test_targets_for("core/src/dsp/RealFft.cpp", {}) == ["rta_core_tests"]


def test_platform_types_maps_before_platform():
    assert mutation_targets.test_targets_for("platform/types/src/ChannelConfig.cpp", {}) == [
        "rta_platform_tests"
    ]
    assert mutation_targets.test_targets_for("platform/src/AudioIo.cpp", {}) == [
        "rta_platform_juce_tests"
    ]


def test_ui_path_maps_to_az_ui_tests():
    assert mutation_targets.test_targets_for("ui/src/GridPanel.cpp", {}) == ["az_ui_tests"]


def test_app_src_uses_parsed_map_first():
    app_map = {"SplAlarms.cpp": "rtatool_analysis_tests"}
    assert mutation_targets.test_targets_for("app/src/measure/SplAlarms.cpp", app_map) == [
        "rtatool_analysis_tests"
    ]


def test_app_src_falls_back_when_unmapped(capsys):
    result = mutation_targets.test_targets_for("app/src/measure/BrandNewFile.cpp", {})
    assert result == ["rtatool_analysis_tests"]
    captured = capsys.readouterr()
    assert "WARNING" in captured.err


def test_unknown_path_returns_empty():
    assert mutation_targets.test_targets_for("docs/dsp/notes.md", {}) == []


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
