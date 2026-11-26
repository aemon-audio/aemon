// A picture of the card "Keep this one for mixing" as it appears the first time, then closed: from then on the delay stands in the bar. AGPL-3.0-or-later.
#pragma once
#include <aemon_core/aemon_core.h>

inline void shotCard (aemon::Shell& sh, juce::Component& ed, const juce::File& f)
{
    const auto was = aemon::Shell::seenFile; const bool cards = aemon::Shell::delayCards;
    aemon::Shell::seenFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("aemon-shot", ".txt", false); aemon::Shell::delayCards = true; sh.checkDelay();
    if (auto* c = sh.getDelayCard(); c != nullptr && c->isVisible())
    {
        const auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f); f.deleteFile(); { juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os); }
        if (c->onGotIt) c->onGotIt();
        sh.checkDelay();
    }
    aemon::Shell::seenFile.deleteFile(); aemon::Shell::seenFile = was; aemon::Shell::delayCards = cards;
}
