// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only).
// Decision record: docs/dsp/2026-10-21-analyser-rebuild.md, section 6.
#pragma once

#include <juce_core/juce_core.h>

#include <cstdio>

namespace rta::measure {

/// How long a thread gets to answer a stop request before the overrun is logged.
/// A diagnostic threshold, NOT a kill deadline -- see `stopThreadNoKill`.
inline constexpr int kThreadStopOverrunMs = 2000;

/// Stops `thread` and joins it. Never force-kills it.
///
/// `juce::Thread::stopThread(2000)` TerminateThread()s a thread that has not
/// exited in 2 s (juce_Thread.cpp, "killing thread by force"). A killed thread
/// can die holding the CRT heap lock, the loader lock or an allocator arena, and
/// the next thing the process does -- the destructor chain, then exit() -- takes
/// the same lock and hangs. That is the leading suspect for the intermittent
/// test-process exit hang under load, so the kill is removed rather than tuned.
///
/// Why an unbounded join is correct here and not just brave: both callers' loops
/// do a bounded amount of work per iteration and test `threadShouldExit()`
/// between iterations (AnalysisThread: one Analyser build or one hop, SyntheticInput:
/// one block), and `notify()` wakes their `wait()`. So the join is as long as one
/// iteration, tens of milliseconds, and an unbounded join only exists to make the
/// failure mode "slow and logged" instead of "killed with a lock held". A thread
/// that really never exits was a hang before this change too; now it hangs where
/// the cause is visible (the log line below, a debugger on the joiner) instead of
/// where a killed thread's lock hides it.
///
/// `overrunMs` is a parameter only so a test can reach the overrun branch without
/// waiting two seconds. Returns the milliseconds the join took, so a test can
/// compare it with the bound it derived.
inline juce::uint32 stopThreadNoKill(juce::Thread& thread, int overrunMs = kThreadStopOverrunMs) {
    const juce::uint32 start = juce::Time::getMillisecondCounter();
    thread.signalThreadShouldExit();
    thread.notify();
    if (!thread.waitForThreadToExit(overrunMs)) {
        juce::Logger::writeToLog("!! thread '" + thread.getThreadName() + "' has not exited in " +
                                 juce::String(overrunMs) +
                                 " ms; joining without a force-kill");
        // Logger has no sink unless the app installs one (OutputDebugString only):
        // stderr is what a human running the exe or ctest actually sees.
        std::fprintf(stderr, "!! thread '%s' has not exited in %d ms; joining without a force-kill\n",
                     thread.getThreadName().toRawUTF8(), overrunMs);
        thread.waitForThreadToExit(-1);
    }
    // Already exited: only releases the handle. `-1` and not a timeout because
    // stopThread() kills whatever is still running when its timeout expires.
    thread.stopThread(-1);
    return juce::Time::getMillisecondCounter() - start;
}

}  // namespace rta::measure
