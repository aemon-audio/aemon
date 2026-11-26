// What the words of every style preset promise, as a direction: brighter, darker, thinner, less boom, snap kept, heard on phones.
// These were written from the words before anything was measured, and are not read off the result: a preset that breaks one is
// changed, or its words are. Rules that turned out to measure something else were replaced, and these are: crest for the punch of a word (it also falls
// when loud words are held back; now the first 15 ms of words that start out of a pause; the same for "without grip",
// which was the words measure, and that marks a voice down for being held at all), hiss for "shine kept" (to a de-esser hiss is an S; now the vowels and the air over 12 kHz),
// sides of a mix without low sides for "mono 808" (now an 808 with low sides), the spread of the top of a voice for "every band" (it is S or nothing; now
// notes in every band), the top of a whole voice for an edge (the S hides it; now the harmonics of two tones), and "Drill 808" was read as dirt where it says clean.
// (StyleRows.h holds the other kind: what was measured, boxed in, so that nothing moves unnoticed.)
// AGPL-3.0-or-later.
#pragma once
#include "StyleKit.h"
namespace rules
{
using R = std::vector<style::Row>;
constexpr double no = -999, any = 999;
inline const R& of (const juce::String& plugin)
{
    static const std::map<juce::String, R> all {
        { "Utility", {
            { "Trap ad-libs: left", { { "loud", no, -1 }, { "side", 2, any } } },
            { "Trap ad-libs: right", { { "loud", no, -1 }, { "side", 2, any } } },
            { "Hook stack: wide", { { "loud", no, -1 }, { "side", 3, any } } },
            { "Beat under a voice", { { "loud", -3, -1 }, { "side low", no, 0.5 } } } } },
        { "EQ", {
            { "Boom bap lead", { { "curve 150-350", -0.5, any }, { "curve 2000-5000", 0.5, any }, { "curve 10000-14000", no, -0.5 } } },      // thick, forward, no fizz
            { "90s East Coast lead", { { "curve 40-110", no, -1 }, { "curve 10000-14000", no, -1 } } },                                        // in the mids
            { "West Coast lead", { { "curve 10000-14000", 0.5, any } } },                                                                      // open
            { "Southern slowed voice", { { "curve 40-110", 1, any }, { "curve 10000-14000", no, -2 } } },                                      // deep, dark
            { "Memphis tape voice", { { "curve 40-110", no, -6 }, { "curve 10000-14000", no, -6 } } },                                         // narrow
            { "Trap lead", { { "curve 10000-14000", 1, any }, { "curve 2000-5000", 1, any }, { "curve 150-350", no, -1 } } },                  // bright, in front, tight
            { "Trap ad-libs", { { "curve 40-110", no, -10 }, { "curve 150-350", no, -1 }, { "curve 2000-5000", 1, any } } },                   // thin, wide awake
            { "Drill lead", { { "curve 10000-14000", no, -1 }, { "curve 2000-5000", 0.3, any } } },                                            // dark but clear
            { "Melodic rap lead", { { "curve 10000-14000", 1, any } } },                                                                       // polished
            { "Cloud rap voice", { { "curve 2000-5000", no, -0.5 }, { "curve 150-350", no, 0 } } },                                            // far: less presence, less chest
            { "Rage lead", { { "curve 1500-5500", 0.8, any }, { "curve 250-500", no, -1 } } },                                                 // sharp, the mud out
            { "Pop lead", { { "curve 40-110", no, -2 }, { "curve 10000-14000", 2, any } } },                                                   // clean low, shiny
            { "R&B lead", { { "curve 150-350", 0, any }, { "curve 10000-14000", 1, any } } },                                                  // warm body, silk on top
            { "Gospel hook stack", { { "curve 150-350", -2.5, -0.5 }, { "curve 2000-5000", 0.5, any }, { "curve 10000-14000", 0.5, any } } },  // less cloud but not thin, clear, open
            { "Afrobeats lead", { { "curve 40-110", no, -3 }, { "curve 150-350", no, -1 }, { "curve 2000-5000", 0.5, any } } },                // light, clear
            { "70s soul voice", { { "curve 150-350", 0.2, any }, { "curve 10000-14000", no, -1 } } },                                          // warm, soft top
            { "80s lead", { { "curve 10000-14000", 1, any }, { "curve 300-500", no, -1 } } },                                                  // crisp, tight
            { "Whisper layer", { { "curve 40-110", no, -20 }, { "curve 150-350", no, -5 }, { "curve 10000-14000", 3, any } } },                // only breath and edge
            { "Boomy close microphone", { { "curve 150-220", no, -2 }, { "curve 1500-5000", -1, 1 } } },                                       // boom gone, voice stays
            { "Harsh cheap microphone", { { "curve 3000-4000", no, -1.5 }, { "curve 150-600", -1, 1 } } },                                     // needles out
            { "Nasal voice", { { "curve 250-500", 1, any }, { "curve 2000-5000", 1, any } } },                                                 // 1 kHz, the nose, lower than all around it
            { "Untreated room", { { "curve 250-350", no, -1 }, { "curve 450-650", no, -0.5 }, { "curve 10000-14000", 0.2, any } } },           // box and ring out, top open
            { "Boom bap drums", { { "curve 40-110", 0.5, any }, { "curve 10000-14000", no, -1 } } },
            { "90s East Coast beat", { { "curve 40-110", 1, any }, { "curve 10000-14000", no, -1 } } },
            { "West Coast beat", { { "curve 40-110", 0.8, any }, { "curve 10000-14000", 0.5, any } } },
            { "Southern slowed beat", { { "curve 40-110", 2, any }, { "curve 10000-14000", no, -2 } } },
            { "Memphis beat", { { "curve 25-50", no, -2 }, { "curve 10000-14000", no, -5 } } },
            { "Trap drums", { { "curve 40-80", 1, any }, { "curve 3000-6000", 1, any }, { "curve 250-500", no, -2 } } },                       // sub, snap, nothing between
            { "Drill drums", { { "curve 50-100", 1, any }, { "curve 10000-14000", no, -1 } } },
            { "Gospel sample", { { "curve 250-350", no, -1 }, { "curve 40-80", no, -2 }, { "curve 8000-14000", 0.5, any } } },
            { "House drums", { { "curve 45-80", 1, any }, { "curve 10000-16000", 1.5, any } } },
            { "Lo-fi beat", { { "curve 10000-14000", no, -3 }, { "curve 25-50", no, -2 } } },
            { "80s drums", { { "curve 90-160", 1, any }, { "curve 2500-3600", 1.5, any } } } } },
        { "Compressor", {
            { "Boom bap lead", { { "spread", 0.5, 7 }, { "onset", 0, any } } },                   // steady, still breathing: the punch of every word stays
            { "90s East Coast lead", { { "spread", 1.5, any } } },                                // firm
            { "West Coast lead", { { "spread", 1, any }, { "onset", -0.5, any } } },              // even, without grip
            { "Trap lead", { { "spread", 3, any } } },                                            // pinned
            { "Trap ad-libs", { { "spread", 3, any }, { "crest", no, -1 } } },                    // flat
            { "Drill lead", { { "spread", 1.5, any }, { "onset", 0.5, any } } },                  // tight, words bite
            { "Melodic rap lead", { { "spread", 1.5, any } } },
            { "Pop lead", { { "spread", 1.5, any } } },
            { "R&B lead", { { "spread", 0.2, 4 } } },                                             // soft hands
            { "Rage lead", { { "spread", 3, any }, { "crest", no, -1 } } },                       // crushed
            { "Gospel hook stack", { { "spread", 1.5, any } } },
            { "Whisper layer", { { "spread", 3, any } } },                                        // every breath up
            { "Spoken intro", { { "spread", 1.5, any } } },
            { "Already tuned", { { "spread", -1, 1.5 }, { "onset", -1, 1 } } },                  // barely touch it
            { "Boom bap drums", { { "snap", 1, any } } },                                         // the hit goes through, the rest closes behind it
            { "West Coast beat", { { "pump", -4, -0.2 } } },                                      // slow glue
            { "Trap drums", { { "snap", -0.5, any } } },                                          // keep the snap
            { "House drums", { { "pump", no, -1.5 }, { "back", -1, any } } },                     // breathes with the kick, back before the next
            { "Lo-fi beat", { { "pump", no, -1.5 } } },                                           // pump
            { "70s soul loop", { { "pump", -4, -0.2 } } },
            { "80s drums", { { "pump", no, -2 } } } } },                                          // big squeeze
        { "Limiter", {
            { "Rap vocal bus", { { "peak", no, -5.9 } } },                                        // 6 dB under full scale
            { "Gospel hook stack", { { "peak", no, -5.9 } } },
            { "Boom bap master", { { "loud", 1, any }, { "pump", -3, any } } },                   // still moving
            { "West Coast master", { { "loud", 2, any }, { "pump", -4, any } } },                 // smooth
            { "Trap master", { { "loud", 4, any }, { "808 phone", -1.5, 1.5 } } },                // 808 intact: no more harmonics against its sub than it came with
            { "Rage master", { { "loud", 7, any }, { "pump", no, -2 } } },                          // everything leans on the kick
            { "House master", { { "pump", no, -1.5 }, { "back", -1, any } } },                    // kick in charge, recovers within a beat
            { "Lo-fi beat tape", { { "peak", no, -2.9 } } } } },
        { "Clipper", {
            { "Trap lead", { { "crest", no, -0.5 }, { "words", 0.9, any } } },
            { "Rage lead", { { "crest", no, -2 } } },
            { "Screamed voice", { { "crest", no, -1 } } },
            { "Boom bap drums", { { "crest", no, -1.5 } } },
            { "Trap drums", { { "crest", no, -1 }, { "snap", -1.5, any } } },                     // loud kit, snap kept
            { "Trap 808", { { "loud", 2, any }, { "808 phone", 3, any }, { "808 sub", -1, any } } },      // grows into its harmonics
            { "Drill 808", { { "loud", 1, any }, { "808 phone", -1, 2 } } },                          // solid, and the slides stay clean
            { "Memphis beat", { { "harm", 4, any } } },
            { "House drums", { { "crest", -5, -0.3 } } } } },
        { "Saturation", {
            { "Boom bap lead", { { "harm", 1, any } } }, { "90s East Coast lead", { { "harm", 1, any } } }, { "West Coast lead", { { "harm", 0.3, 8 } } }, { "Memphis tape voice", { { "harm", 4, any } } },
            { "Trap lead", { { "harm", 1, any }, { "harm high", 0.5, any } } }, { "Drill lead", { { "harm", 0.3, 8 } } }, { "Melodic rap lead", { { "harm", 0.2, 3 }, { "harm high", 0.2, 3 } } }, { "Rage lead", { { "harm", 8, any } } }, { "R&B lead", { { "harm", 0.1, 5 } } },
            { "Screamed voice", { { "harm", 2, any } } }, { "70s soul voice", { { "harm", 1.5, any } } }, { "Boom bap drums", { { "harm", 2, any } } },
            { "Memphis beat", { { "808 phone", 6, any } } },                                      // the low end breaks up
            { "Phonk drums", { { "harm", 5, any } } },
            { "Trap 808", { { "808 phone", 6, any }, { "808 sub", -1, 1.5 } } },                  // heard on phones, sub kept
            { "Gospel sample", { { "harm", 0.3, 10 } } }, { "House drums", { { "harm", 1, any } } }, { "Lo-fi beat", { { "harm", 1, any } } } } },
        { "De-esser", {
            { "Trap lead", { { "ess", no, -0.5 }, { "vowels", -0.7, 0.7 } } }, { "Melodic rap lead", { { "ess", no, -0.3 } } }, { "R&B lead", { { "ess", -4, -0.2 } } }, { "Pop lead", { { "ess", no, -0.5 }, { "vowels", -0.7, 0.7 }, { "tone 12000-16000", -2.5, any } } },
            { "Boom bap lead", { { "ess", -2, -0.05 } } }, { "Drill lead", { { "ess", no, -0.3 } } }, { "Trap ad-libs", { { "ess", no, -1 } } }, { "Gospel hook stack", { { "ess", no, -1 } } }, { "Harsh cheap microphone", { { "ess", no, -0.8 } } }, { "Whisper layer", { { "ess", no, -0.8 } } } } },
        { "Imager", {
            { "Trap lead", { { "side low", no, 0.5 }, { "side high", 1, any } } }, { "Trap ad-libs", { { "side", 2, any } } }, { "Melodic rap doubles", { { "side", 0.5, any } } }, { "R&B stacks", { { "side", 1, any } } }, { "Gospel hook stack", { { "side", 2, any } } },
            { "Whisper layer", { { "side high", 2, any } } }, { "Telephone intro", { { "side", no, -20 } } },
            { "West Coast beat", { { "side", 0.5, any }, { "808 side", no, -6 } } }, { "Trap beat", { { "808 side", no, -12 }, { "side high", 1, any } } }, { "Memphis beat", { { "side", no, -6 } } }, { "Cloud rap beat", { { "side", 2, any } } }, { "Lo-fi beat", { { "side", no, -2 }, { "side low", no, -2 } } } } },
        { "Multiband", {
            { "Boom bap lead", { { "spread", 0.3, any } } }, { "90s East Coast lead", { { "spread", 0.3, any } } }, { "Trap lead", { { "spread", 1, any }, { "hold 120", 0.5, any }, { "hold 400", 0.5, any }, { "hold 2500", 0.5, any }, { "hold hats", 0.5, any } } }, { "Melodic rap lead", { { "spread", 0.5, any } } },
            { "Drill lead", { { "spread low", 0.5, any } } }, { "R&B lead", { { "spread", 0.2, any }, { "hold 120", 0.3, any }, { "hold 400", 0.3, any }, { "hold 2500", 0.3, any }, { "hold hats", 0.3, any } } }, { "Rage lead", { { "spread", 1.5, any } } }, { "Pop lead", { { "spread", 0.5, any } } }, { "Gospel hook stack", { { "spread", 0.7, any } } },
            { "Boom bap drums", { { "hold sub", 0.5, any }, { "hold hats", no, 0.5 } } },         // knock held, hats loose
            { "West Coast beat", { { "hold sub", 0.5, any }, { "hold hats", no, 1.5 } } },
            { "Trap beat", { { "hold sub", 0.5, any }, { "hold hats", 0.5, any } } },             // sub steady, hats calm
            { "Memphis beat", { { "hold sub", 0.5, any }, { "spread", 0.5, any } } },
            { "House groove", { { "pump low", no, -1 }, { "back low", -1.5, any } } },            // gives way to the kick and comes back in time
            { "Lo-fi beat", { { "hold sub", 0.3, any }, { "hold hats", 0.3, any } } } } } };
    static const R none; const auto it = all.find (plugin); return it == all.end() ? none : it->second;
}
} // namespace rules
