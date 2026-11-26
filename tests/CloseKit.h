// A plugin is made, used, changed and closed again and again while the computer is as busy as it can be. AGPL-3.0-or-later.
// A background thread that is stopped with a time limit is killed by force when it gets no time, and may take a Windows lock with it: the whole
// program then hangs for ever. The published EQ did that.
#pragma once
#include "EffectTestKit.h"
#include <thread>

namespace closekit
{
struct Ears : juce::Logger { std::atomic<int> killed { 0 }; void logMessage (const juce::String& m) override { if (m.contains ("killing thread by force")) ++killed; } };
/** use: prepares and changes it, with or without its window. */
template <typename P> void closeUnderLoad (const char* name, int times, const std::function<void (P&, int)>& use, const std::function<void (bool, const juce::String&, const juce::String&)>& check)
{
    Ears ears; juce::Logger::setCurrentLogger (&ears); std::atomic<bool> stop { false }; std::vector<std::thread> busy; const unsigned n = 3 * juce::jmax (2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < n; ++i) busy.emplace_back ([&] { volatile double x = 1; while (! stop) for (int k = 0; k < 1000; ++k) x = x * 1.0000001 + 1e-9; });
    double worst = 0; for (int i = 0; i < times && ears.killed == 0; ++i) { auto p = std::make_unique<P>(); use (*p, i); const auto a = juce::Time::getMillisecondCounterHiRes(); p.reset(); worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - a); }
    stop = true; for (auto& t : busy) t.join();
    juce::Logger::setCurrentLogger (nullptr); check (ears.killed == 0, juce::String (name) + ": closed " + juce::String (times) + " times on a busy computer, no thread killed by force", ears.killed > 0 ? juce::String ("KILLED BY FORCE") : "longest close " + juce::String (worst, 0) + " ms with " + juce::String ((int) n) + " busy threads");
}
} // namespace closekit
