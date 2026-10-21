// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. view/FaultLine.h: which fault the device
// panel's one slot shows when the audio device AND the analysis thread can
// each fault. The reader itself (DevicePanel on a real MainComponent) is
// tested in tests_juce/test_main_component_fault_slot.cpp.
#include "view/FaultLine.h"

#include <catch2/catch_test_macros.hpp>

using rta::platform::Fault;
using rta::view::selectFault;

namespace {
Fault fault(Fault::Kind kind, const char* message, std::uint32_t sequence) {
    Fault f;
    f.kind = kind;
    f.message = message;
    f.sequence = sequence;
    return f;
}
}  // namespace

TEST_CASE("no fault anywhere shows nothing", "[fault_line]") {
    const Fault out = selectFault(Fault{}, Fault{});
    CHECK(out.kind == Fault::Kind::None);
    CHECK(out.message.empty());
    CHECK(out.sequence == 0);
}

TEST_CASE("an analysis-thread fault is shown, and labelled as such", "[fault_line]") {
    const Fault out = selectFault(Fault{}, fault(Fault::Kind::DeviceError, "bad_alloc", 1));
    CHECK(out.kind == Fault::Kind::DeviceError);
    CHECK(out.message == "ANALYSIS STOPPED: bad_alloc");
}

TEST_CASE("a device fault wins over an analysis fault", "[fault_line]") {
    const Fault out = selectFault(fault(Fault::Kind::OpenFailed, "no such device", 2),
                                  fault(Fault::Kind::DeviceError, "bad_alloc", 1));
    CHECK(out.kind == Fault::Kind::OpenFailed);
    CHECK(out.message == "no such device");
}

TEST_CASE("the sequence moves whenever EITHER source records something", "[fault_line]") {
    // DevicePanel repaints when this differs from the last one it painted, so
    // a new analysis fault under a healthy device (audio sequence unchanged)
    // must still change it -- and so must a new device fault.
    const auto base = selectFault(fault(Fault::Kind::None, "", 3), Fault{}).sequence;
    CHECK(selectFault(fault(Fault::Kind::None, "", 3),
                      fault(Fault::Kind::DeviceError, "x", 1)).sequence != base);
    CHECK(selectFault(fault(Fault::Kind::DeviceStopped, "y", 4), Fault{}).sequence != base);
}

TEST_CASE("an unacknowledged capture resync is shown when nothing else is faulted", "[fault_line]") {
    const Fault out = selectFault(Fault{}, Fault{}, 2);
    CHECK(out.kind == Fault::Kind::DeviceError);
    CHECK(out.message.rfind("CAPTURE RESYNC UNACKNOWLEDGED x2", 0) == 0);
    CHECK(selectFault(Fault{}, Fault{}, 0).message.empty());
}

TEST_CASE("a real fault outranks the resync note, and the sequence still moves with it",
          "[fault_line]") {
    const Fault device = fault(Fault::Kind::DeviceStopped, "unplugged", 1);
    const Fault out = selectFault(device, Fault{}, 3);
    CHECK(out.message == "unplugged");
    CHECK(selectFault(Fault{}, fault(Fault::Kind::DeviceError, "bad_alloc", 1), 3).message ==
          "ANALYSIS STOPPED: bad_alloc");
    // DevicePanel repaints only when this differs from the last one painted: a
    // fourth timeout under an existing fault must still be a new sequence.
    CHECK(selectFault(device, Fault{}, 4).sequence != out.sequence);
    CHECK(selectFault(Fault{}, Fault{}, 1).sequence != selectFault(Fault{}, Fault{}, 0).sequence);
}
