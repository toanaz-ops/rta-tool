// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RawCaptureBuffer's own unit coverage, split out of test_delay_locator.cpp
// when DelayLocator (lane L7-DELAY task F1) was deleted as superseded --
// MainComponentDelay.cpp reimplements the arm/wait/capture flow by polling
// OutputEngine telemetry and calling rta::dsp::suggestDelay directly,
// because DelayLocator::feedHop() needed per-hop audio that only
// AnalysisThread's drain loop sees. RawCaptureBuffer itself was NOT
// DelayLocator-only: AnalysisThread::armLocateCapture (measure/
// AnalysisThread.h) owns one (`locateBuffer_`) as its raw-capture
// accumulator, so these two properties -- exact accumulation order and no
// mid-capture allocation -- still need direct coverage of the class itself,
// not just the indirect exercise app/tests_juce/test_delay_locate.cpp gives
// it through a real AnalysisThread.
#include "measure/RawCaptureBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace rta::measure;

TEST_CASE("the accumulator yields exactly L", "[delay][rawcapture]") {
    RawCaptureBuffer buffer;
    buffer.arm(4096);

    std::vector<float> refA(300, 1.0f), measA(300, -1.0f);
    std::vector<float> refB(3000, 2.0f), measB(3000, -2.0f);
    std::vector<float> refC(2000, 3.0f), measC(2000, -3.0f);  // overshoots by 1204

    buffer.feedHop(refA, measA);
    REQUIRE_FALSE(buffer.isFull());
    buffer.feedHop(refB, measB);
    REQUIRE_FALSE(buffer.isFull());
    buffer.feedHop(refC, measC);
    REQUIRE(buffer.isFull());

    REQUIRE(buffer.reference().size() == 4096);
    REQUIRE(buffer.measurement().size() == 4096);
    // In order, no gap, no double-count: the first 300 read 1.0/-1.0, the
    // next 3000 read 2.0/-2.0, the remaining 796 (of C's 2000) read 3.0/-3.0.
    REQUIRE(buffer.reference()[0] == 1.0f);
    REQUIRE(buffer.reference()[299] == 1.0f);
    REQUIRE(buffer.reference()[300] == 2.0f);
    REQUIRE(buffer.reference()[3299] == 2.0f);
    REQUIRE(buffer.reference()[3300] == 3.0f);
    REQUIRE(buffer.reference()[4095] == 3.0f);
    REQUIRE(buffer.measurement()[0] == -1.0f);
    REQUIRE(buffer.measurement()[4095] == -3.0f);
}

TEST_CASE("the accumulator does not allocate mid-capture", "[delay][rawcapture]") {
    // Same PROPERTY test_average_group.cpp's T12 measures with a counting
    // global operator new -- not reused here because that override is
    // already installed, once, for this whole test BINARY
    // (rtatool_analysis_tests), and a second definition would be a link
    // error. Pointer stability is the direct, allocator-agnostic proof for
    // ONE class's own buffers: if `arm()` is the only allocation, the
    // vector's storage address never moves across `feedHop()`.
    RawCaptureBuffer buffer;
    buffer.arm(4096);
    const float* refBefore = buffer.reference().data();
    const float* measBefore = buffer.measurement().data();

    std::vector<float> ref(512, 0.5f), meas(512, -0.5f);
    for (int i = 0; i < 8; ++i) buffer.feedHop(ref, meas);

    REQUIRE(buffer.isFull());
    CHECK(buffer.reference().data() == refBefore);
    CHECK(buffer.measurement().data() == measBefore);
}
