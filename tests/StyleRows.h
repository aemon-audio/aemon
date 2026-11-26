// What every style preset measures, boxed in (StyleKit.h). These ranges were read off the result, with room for the differences between voices: they prove
// that nothing moves unnoticed, not that a preset does what its words say. What the words promise is in StyleRules.h, written before measuring. AGPL-3.0-or-later.
#pragma once
#include "StyleKit.h"
#include "StyleRules.h"
namespace rows
{
using R = std::vector<style::Row>;
inline R utility()
{
    return {
        { "Trap ad-libs: left, tucked back", { { "loud", -3.1, -2.5 }, { "side", 3.2, 5.9 } } },
        { "Trap ad-libs: right, tucked back", { { "loud", -9.9, -9.3 }, { "side", 4.4, 5.7 } } },
        { "Hook stack: wide, behind the lead", { { "loud", -2.8, -1.9 }, { "side", 5.5, 6.5 } } },
        { "Beat under a voice: 2 dB back, bass in the middle", { { "loud", -2.4, -1.8 }, { "side", -0.5, 0.5 } } },
    };
}
inline R eq()
{
    return {
        { "Boom bap lead: thick, forward, no fizz", { { "curve 40-110", -4.6, -3.6 }, { "curve 150-350", -0.3, 0.7 }, { "curve 2000-5000", 1.1, 2.1 }, { "curve 10000-14000", -1.8, -0.8 } } },
        { "90s East Coast lead: raw, in the mids", { { "curve 40-110", -10.6, -9.6 }, { "curve 150-350", -3.5, -2.5 }, { "curve 2000-5000", 0, 1 }, { "curve 10000-14000", -3.5, -2.5 } } },
        { "West Coast lead: smooth and open", { { "curve 40-110", -4.3, -3.3 }, { "curve 150-350", -2.1, -1.1 }, { "curve 2000-5000", 0.7, 1.7 }, { "curve 10000-14000", 1.1, 2.1 } } },
        { "Southern slowed voice: deep and dark", { { "curve 40-110", 2.3, 3.3 }, { "curve 150-350", 0.4, 1.4 }, { "curve 2000-5000", -2, -1 }, { "curve 10000-14000", -6.8, -5.8 } }, "d" },
        { "Memphis tape voice: narrow and worn", { { "curve 40-110", -25.7, -24.7 }, { "curve 150-350", -3.3, -2.3 }, { "curve 2000-5000", -2.4, -1.4 }, { "curve 10000-14000", -26.1, -25.1 } }, "dx" },
        { "Trap lead: bright, tight, in front", { { "curve 40-110", -8.8, -7.8 }, { "curve 150-350", -2.8, -1.8 }, { "curve 2000-5000", 1.6, 2.6 }, { "curve 10000-14000", 2.3, 3.3 } } },
        { "Trap ad-libs: thin and wide awake", { { "curve 40-110", -27.4, -26.4 }, { "curve 150-350", -2.5, -1.5 }, { "curve 2000-5000", 2.1, 3.1 }, { "curve 10000-14000", 1.1, 2.1 } } },
        { "Drill lead: dark but clear", { { "curve 40-110", -8.1, -7.1 }, { "curve 150-350", -3, -2 }, { "curve 2000-5000", 0.6, 1.6 }, { "curve 10000-14000", -3.5, -2.5 } } },
        { "Melodic rap lead: tuned voice, polished", { { "curve 40-110", -10.7, -9.7 }, { "curve 150-350", -1.6, -0.6 }, { "curve 2000-5000", -1, 0 }, { "curve 10000-14000", 1.8, 2.8 } } },
        { "Cloud rap voice: soft and far", { { "curve 40-110", -21.3, -20.3 }, { "curve 150-350", -1.2, -0.2 }, { "curve 2000-5000", -2, -1 }, { "curve 10000-14000", 0.9, 1.9 } }, "d" },
        { "Rage lead: sharp and loud in the mids", { { "curve 40-110", -16.5, -15.5 }, { "curve 150-350", -3, -2 }, { "curve 2000-5000", 1, 2 }, { "curve 10000-14000", -0.9, 0.1 } } },
        { "Pop lead: clean low, shiny top", { { "curve 40-110", -6.5, -5.5 }, { "curve 150-350", -1.4, -0.4 }, { "curve 2000-5000", 1.4, 2.4 }, { "curve 10000-14000", 2.6, 3.6 } } },
        { "R&B lead: warm body, silk on top", { { "curve 40-110", -1.6, -0.6 }, { "curve 150-350", -0.2, 0.8 }, { "curve 2000-5000", -1.3, -0.3 }, { "curve 10000-14000", 1.2, 2.2 } } },
        { "Gospel hook stack: clear and open", { { "curve 40-110", -11.2, -10.2 }, { "curve 150-350", -2.4, -1.4 }, { "curve 2000-5000", 1.1, 2.1 }, { "curve 10000-14000", 0.9, 1.9 } } },
        { "Afrobeats lead: light and clear", { { "curve 40-110", -15.9, -14.9 }, { "curve 150-350", -3.1, -2.1 }, { "curve 2000-5000", 0.9, 1.9 }, { "curve 10000-14000", 0.4, 1.4 } } },
        { "70s soul voice: warm and close", { { "curve 40-110", -0.4, 0.6 }, { "curve 150-350", 0.2, 1.2 }, { "curve 2000-5000", -0.3, 0.7 }, { "curve 10000-14000", -4.3, -3.3 } } },
        { "80s lead: crisp and polished", { { "curve 40-110", -10.7, -9.7 }, { "curve 150-350", -1.3, -0.3 }, { "curve 2000-5000", -0.1, 0.9 }, { "curve 10000-14000", 2.6, 3.6 } } },
        { "Whisper layer: only breath and edge", { { "curve 40-110", -48.2, -47.2 }, { "curve 150-350", -9.8, -8.8 }, { "curve 2000-5000", 2.6, 3.6 }, { "curve 10000-14000", 6.2, 7.2 } }, "bx" },
        { "Boomy close microphone: boom gone, voice stays", { { "curve 40-110", -4.9, -3.9 }, { "curve 150-350", -4.2, -3.2 }, { "curve 2000-5000", -0.2, 0.8 }, { "curve 10000-14000", -0.2, 0.8 } } },
        { "Harsh cheap microphone: needles out", { { "curve 40-110", -5.1, -4.1 }, { "curve 150-350", -0.4, 0.6 }, { "curve 2000-5000", -2.3, -1.3 }, { "curve 10000-14000", -0.5, 0.5 } } },
        { "Nasal voice: nose out, words stay", { { "curve 40-110", -2.7, -1.7 }, { "curve 150-350", 2, 3 }, { "curve 2000-5000", 3.1, 4.1 }, { "curve 10000-14000", 2.2, 3.2 } } },
        { "Untreated room: box and ring out", { { "curve 40-110", -6.5, -5.5 }, { "curve 150-350", -2.2, -1.2 }, { "curve 2000-5000", 0.2, 1.2 }, { "curve 10000-14000", 0.8, 1.8 } } },
        { "Boom bap drums: dusty and thick", { { "curve 40-110", 0.4, 1.4 }, { "curve 150-350", -0.7, 0.3 }, { "curve 2000-5000", 0.9, 1.9 }, { "curve 10000-14000", -6, -5 } } },
        { "90s East Coast beat: dark and heavy", { { "curve 40-110", 2.1, 3.1 }, { "curve 150-350", 0.3, 1.3 }, { "curve 2000-5000", 0.1, 1.1 }, { "curve 10000-14000", -2.3, -1.3 } } },
        { "West Coast beat: deep bass, clean top", { { "curve 40-110", 1.2, 2.2 }, { "curve 150-350", -1.9, -0.9 }, { "curve 2000-5000", -0.3, 0.7 }, { "curve 10000-14000", 1.1, 2.1 } } },
        { "Southern slowed beat: all low end", { { "curve 40-110", 2.6, 3.6 }, { "curve 150-350", -0.2, 0.8 }, { "curve 2000-5000", -0.7, 0.3 }, { "curve 10000-14000", -9.4, -8.4 } } },
        { "Memphis beat: cassette narrow", { { "curve 40-110", -3.5, -2.5 }, { "curve 150-350", -1.9, -0.9 }, { "curve 2000-5000", -1.9, -0.9 }, { "curve 10000-14000", -20, -19 } } },
        { "Trap drums: sub and snap, nothing between", { { "curve 40-110", 1.1, 2.1 }, { "curve 150-350", -2.3, -1.3 }, { "curve 2000-5000", 1.1, 2.1 }, { "curve 10000-14000", 2.1, 3.1 } } },
        { "Drill drums: sliding bass up front", { { "curve 40-110", 1.4, 2.4 }, { "curve 150-350", -1.7, -0.7 }, { "curve 2000-5000", -0.5, 0.5 }, { "curve 10000-14000", -2, -1 } } },
        { "Gospel sample: old loop opened up", { { "curve 40-110", -5.7, -4.7 }, { "curve 150-350", -2.1, -1.1 }, { "curve 2000-5000", 0.4, 1.4 }, { "curve 10000-14000", 1.3, 2.3 } } },
        { "House drums: kick weight, hat air", { { "curve 40-110", 0.8, 1.8 }, { "curve 150-350", -2.5, -1.5 }, { "curve 2000-5000", -0.3, 0.7 }, { "curve 10000-14000", 1.3, 2.3 } } },
        { "Lo-fi beat: soft and worn", { { "curve 40-110", -2.2, -1.2 }, { "curve 150-350", -0.5, 0.5 }, { "curve 2000-5000", -1, 0 }, { "curve 10000-14000", -8.8, -7.8 } } },
        { "80s drums: big crack", { { "curve 40-110", 0.8, 1.8 }, { "curve 150-350", -0.3, 0.7 }, { "curve 2000-5000", 1.8, 2.8 }, { "curve 10000-14000", 1.2, 2.2 } } },
    };
}
inline R comp()
{
    return {
        { "Boom bap lead: steady, still breathing", { { "spread", 2.6, 8.2 }, { "crest", -4.9, -0.4 } } },
        { "90s East Coast lead: firm and raw", { { "spread", 2.5, 8.2 }, { "crest", -5.5, -2.4 } } },
        { "West Coast lead: smooth and even", { { "spread", 2.1, 5.6 }, { "crest", -3.3, 0.8 } } },
        { "Trap lead: pinned in front", { { "spread", 3, 10.9 }, { "crest", -7.9, -4.7 } } },
        { "Trap ad-libs: flat and loud", { { "spread", 3.1, 11.6 }, { "crest", -9.2, -5.2 } }, "d" },
        { "Drill lead: tight, words bite", { { "spread", 3.1, 10.6 }, { "crest", -3.9, 0.2 } } },
        { "Melodic rap lead: floating, never jumps", { { "spread", 2.2, 7.6 }, { "crest", -6.1, -2.6 } } },
        { "Pop lead: polished and close", { { "spread", 2.4, 5.7 }, { "crest", -4.4, -0.7 } } },
        { "R&B lead: soft hands", { { "spread", 1.8, 4.6 }, { "crest", -2.7, 1.2 } } },
        { "Rage lead: crushed and loud", { { "spread", 2.8, 8.8 }, { "crest", -5.2, -1.6 } } },
        { "Gospel hook stack: one voice out of many", { { "spread", 1.9, 6.8 }, { "crest", -3.4, -0.1 } } },
        { "Whisper layer: every breath up", { { "spread", 2.2, 10.3 }, { "crest", -7.4, -3.9 } } },
        { "Spoken intro: late-night radio", { { "spread", 2.2, 8.4 }, { "crest", -4.6, -1.5 } } },
        { "Already tuned and compressed: barely touch it", { { "spread", -0.3, 2.5 }, { "crest", -2.6, 1.2 } } },
        { "Boom bap drums: chopped and thick", { { "spread", 6.4, 8.6 }, { "crest", 4.6, 6.2 } } },
        { "West Coast beat: slow glue", { { "spread", 0.4, 2 }, { "crest", 0.5, 2.1 } } },
        { "Trap drums: keep the snap", { { "spread", 1.8, 3.4 }, { "crest", 1.2, 2.8 } } },
        { "House drums: breathes with the kick", { { "spread", 6.1, 8.3 }, { "crest", 5.4, 7 } } },
        { "Lo-fi beat: tired pump", { { "spread", 0.6, 2.6 }, { "crest", -1.6, 0.4 } } },
        { "70s soul loop: old glue", { { "spread", 0.3, 2.2 }, { "crest", -0.6, 1 } } },
        { "80s drums: big squeeze", { { "spread", 1, 2.6 }, { "crest", 1.2, 2.8 } } },
    };
}
inline R lim()
{
    return {
        { "Rap vocal bus: words pinned", { { "peak", -99, -6 + 0.06 }, { "loud", -0.9, 4.9 }, { "crest", -11.5, -6 } } },
        { "Gospel hook stack: one level", { { "peak", -99, -6 + 0.06 }, { "loud", -2.3, 3.9 }, { "crest", -10.1, -5 } } },
        { "Boom bap master: warm, still moving", { { "peak", -99, -1 + 0.06 }, { "loud", 1.9, 3.1 }, { "crest", -3, -1.4 } } },
        { "West Coast master: smooth and full", { { "peak", -99, -1 + 0.06 }, { "loud", 2.6, 3.8 }, { "crest", -3.6, -2 } } },
        { "Trap master: loud, 808 intact", { { "peak", -99, -1 + 0.06 }, { "loud", 4.5, 5.7 }, { "crest", -5.5, -3.9 } } },
        { "Rage master: blown loud", { { "peak", -99, -1 + 0.06 }, { "loud", 7.2, 8.4 }, { "crest", -8.3, -6.7 } } },
        { "House master: kick in charge", { { "peak", -99, -1 + 0.06 }, { "loud", 5.7, 6.9 }, { "crest", -6.8, -5.2 } } },
        { "Lo-fi beat tape: soft level", { { "peak", -99, -3 + 0.06 }, { "loud", 0.4, 1.6 }, { "crest", -3.5, -1.9 } } },
    };
}
inline R clip()
{
    return {
        { "Trap lead: shouts shaved", { { "peak", -99, -1 + 0.06 }, { "loud", 1.2, 3.6 }, { "crest", -8.6, 0.2 } } },
        { "Rage lead: clipped loud", { { "peak", -99, -2 + 0.06 }, { "loud", 5.6, 10 }, { "crest", -13.9, -7.1 } } },
        { "Screamed voice: peaks held", { { "peak", -99, -1 + 0.06 }, { "loud", 2.7, 5.5 }, { "crest", -10, -1.6 } } },
        { "Boom bap drums: sampler hard", { { "peak", -99, -1 + 0.06 }, { "loud", 5.7, 6.9 }, { "crest", -6.4, -4.8 } } },
        { "Trap drums: loud kit, snap kept", { { "peak", -99, -1 + 0.06 }, { "loud", 6.4, 7.6 }, { "crest", -7.1, -5.5 } } },
        { "Trap 808: fat and loud", { { "peak", -99, -1 + 0.06 }, { "loud", 10.3, 11.5 }, { "crest", -3.6, -2 } } },
        { "Drill 808: solid slide", { { "peak", -99, -2 + 0.06 }, { "loud", 5.3, 6.5 }, { "crest", -1.3, 0.3 } } },
        { "Memphis beat: red-lined", { { "peak", -99, -1 + 0.06 }, { "loud", 9.6, 10.8 }, { "crest", -10.6, -9 } } },
        { "House drums: solid", { { "peak", -99, -1 + 0.06 }, { "loud", 4.1, 5.3 }, { "crest", -4.8, -3.2 } } },
    };
}
inline R sat()
{
    return {
        { "Boom bap lead: sampler grit", { { "harm", 4.1, 5.5 }, { "loud", -0.4, 0.7 } } },
        { "90s East Coast lead: hot and forward", { { "harm", 5.5, 7.5 }, { "loud", -0.1, 1 } } },
        { "West Coast lead: warm body", { { "harm", 3.6, 4.8 }, { "loud", -0.2, 0.8 } } },
        { "Memphis tape voice: worn out", { { "harm", 10.3, 13.9 }, { "loud", -1.3, 0.4 } }, "dx" },
        { "Trap lead: expensive edge", { { "harm", 3.1, 4.1 }, { "loud", -0.4, 0.7 } } },
        { "Drill lead: grain", { { "harm", 2.2, 3 }, { "loud", -0.5, 0.6 } } },
        { "Melodic rap lead: fine grain", { { "harm", 1.2, 2 }, { "loud", -0.4, 0.5 } } },
        { "Rage lead: torn", { { "harm", 8.9, 12.1 }, { "loud", -1.1, 0.3 } }, "x" },
        { "R&B lead: soft harmonics", { { "harm", 2, 2.8 }, { "loud", -0.3, 0.6 } } },
        { "Screamed voice: controlled tear", { { "harm", 4.9, 6.7 }, { "loud", -0.8, 0.4 } } },
        { "70s soul voice: pushed hot", { { "harm", 5.8, 7.8 }, { "loud", 0, 1 } } },
        { "Boom bap drums: sampler crunch", { { "harm", 6.6, 9 }, { "loud", -0.4, 0.4 } } },
        { "Memphis beat: blown speaker", { { "harm", 13.1, 17.7 }, { "loud", -1.9, -1.1 } } },
        { "Phonk drums: driven into the red", { { "harm", 15.6, 21 }, { "loud", -1.4, -0.6 } } },
        { "Trap 808: heard on phones, sub kept", { { "harm", 17.2, 23.2 }, { "loud", -1, -0.2 } } },
        { "Gospel sample: warmed", { { "harm", 3.4, 4.6 }, { "loud", 0, 0.8 } } },
        { "House drums: thick drive", { { "harm", 3.3, 4.5 }, { "loud", -0.2, 0.6 } } },
        { "Lo-fi beat: old recorder", { { "harm", 19.8, 26.8 }, { "loud", 1.1, 1.9 } } },
    };
}
inline R deess()
{
    return {
        { "Trap lead: bright but never sharp", { { "ess", -5.8, -3.2 } } },
        { "Melodic rap lead: tuned voice, smooth S", { { "ess", -5.2, -2.9 } } },
        { "R&B lead: silk", { { "ess", -2.9, -1.1 } } },
        { "Pop lead: S down, shine kept", { { "ess", -4.5, -2.8 } } },
        { "Boom bap lead: just the spikes", { { "ess", -1.7, -0.2 } } },
        { "Drill lead: cold but not sharp", { { "ess", -3.5, -1.5 } } },
        { "Trap ad-libs: S out of the way", { { "ess", -7.3, -4.4 } } },
        { "Gospel hook stack: one S", { { "ess", -8.9, -5.5 } } },
        { "Harsh cheap microphone: needles out", { { "ess", -6.7, -3.9 } } },
        { "Whisper layer: breath stays, S goes", { { "ess", -6.8, -5 } } },
    };
}
inline R imager()
{
    return {
        { "Trap lead: centred, with air", { { "side", -1.2, 3.2 }, { "side high", 2.8, 5.2 }, { "side low", -22.2, -9.4 } } },
        { "Trap ad-libs: wide around the lead", { { "side", 8.4, 10.1 }, { "side high", 8.6, 10.4 }, { "side low", 4.8, 8.9 } } },
        { "Melodic rap doubles: soft sides", { { "side", 3.8, 5.6 }, { "side high", 3.7, 5.6 }, { "side low", 3, 5.4 } } },
        { "R&B stacks: wide and soft", { { "side", 1.7, 3.5 }, { "side high", 1.4, 3.1 }, { "side low", 1.3, 3.7 } } },
        { "Gospel hook stack: choir width", { { "side", 5.8, 7.9 }, { "side high", 5.8, 7.5 }, { "side low", 5.4, 8.2 } } },
        { "Whisper layer: around the head", { { "side", 9.4, 12.8 }, { "side high", 11.8, 13.8 }, { "side low", -5.1, 6.2 } }, "b" },
        { "Telephone intro: dead centre", { { "side", -144.9, -141.6 }, { "side high", -30.8, -29.2 }, { "side low", -30.8, -29.2 } } },
        { "West Coast beat: wide synths, solid low", { { "side", 0.5, 2.1 }, { "side high", 1.7, 3.3 }, { "side low", -1, 0.6 } } },
        { "Trap beat: mono 808, wide hats", { { "side", 0.6, 2.2 }, { "side high", 3.1, 4.7 }, { "side low", -2.4, -0.8 } } },
        { "Memphis beat: almost mono", { { "side", -11.3, -9.7 }, { "side high", -11.3, -9.7 }, { "side low", -11.3, -9.7 } } },
        { "Cloud rap beat: huge pads", { { "side", 3.5, 5.1 }, { "side high", 3.5, 5.1 }, { "side low", 3, 4.6 } } },
        { "Lo-fi beat: narrow and warm", { { "side", -5.2, -3.6 }, { "side high", -5.2, -3.6 }, { "side low", -5.3, -3.7 } } },
    };
}
inline R mb()
{
    return {
        { "Boom bap lead: thick middle, held", { { "spread", 1.7, 4.1 }, { "curve 10000-14000", 0.3, 1.5 } } },
        { "90s East Coast lead: body locked", { { "spread", 2.7, 4.9 }, { "curve 10000-14000", -0.7, 0.5 } } },
        { "Trap lead: every band pinned", { { "spread", 2.7, 5.1 }, { "curve 10000-14000", -1.3, -0.1 } } },
        { "Melodic rap lead: detail lifted", { { "spread", 1.9, 3.8 }, { "curve 10000-14000", -1.2, 0 } } },
        { "Drill lead: chest under control", { { "spread", 1.1, 3.8 }, { "curve 10000-14000", -0.5, 0.7 } } },
        { "R&B lead: silk in every band", { { "spread", 1.1, 3.3 }, { "curve 10000-14000", -1.4, -0.2 } } },
        { "Rage lead: over the top, blended", { { "spread", 1.2, 4 }, { "curve 10000-14000", -2.6, -1.4 } } },
        { "Pop lead: even and shiny", { { "spread", 2.4, 4.2 }, { "curve 10000-14000", -0.3, 0.9 } } },
        { "Gospel hook stack: one big voice", { { "spread", 1.9, 4.3 }, { "curve 10000-14000", -1.8, -0.6 } } },
        { "Boom bap drums: thick knock", { { "spread", 2.8, 4 }, { "curve 10000-14000", 1.3, 2.5 } } },
        { "West Coast beat: steady bass, easy top", { { "spread", 0.8, 2 }, { "curve 10000-14000", 0, 1.2 } } },
        { "Trap beat: sub steady, hats calm", { { "spread", 1.7, 2.9 }, { "curve 10000-14000", -3.1, -1.9 } } },
        { "Memphis beat: squashed cassette", { { "spread", 3.4, 4.6 }, { "curve 10000-14000", -2.6, -1.4 } } },
        { "House groove: low end bounces", { { "spread", 2.1, 3.3 }, { "curve 10000-14000", -0.5, 0.7 } } },
        { "Lo-fi beat: soft and level", { { "spread", 1.6, 2.8 }, { "curve 10000-14000", -2.7, -1.5 } } },
    };
}
} // namespace rows
