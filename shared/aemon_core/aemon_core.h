/*
  Aemon core: shared DSP, metering, state and UI pieces for every Aemon plugin.
  Copyright (C) 2026 Aemon contributors.

  This program is free software: you can redistribute it and/or modify it under the terms
  of the GNU Affero General Public License as published by the Free Software Foundation,
  either version 3 of the License, or (at your option) any later version.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 aemon_core
  vendor:             aemon
  version:            0.1.0
  name:               Aemon core
  description:        Shared DSP, metering, state and UI for Aemon plugins
  license:            AGPLv3
  minimumCppStandard: 20
  dependencies:       juce_audio_processors juce_audio_utils juce_dsp juce_gui_extra

 END_JUCE_MODULE_DECLARATION
*/

#pragma once
#define AEMON_CORE_H_INCLUDED

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include "dsp/aemon_Loudness.h"
#include "dsp/aemon_Meters.h"
#include "state/aemon_History.h"
#include "state/aemon_PluginState.h"
#include "ui/aemon_LookAndFeel.h"
#include "ui/aemon_Controls.h"
#include "ui/aemon_MeterViews.h"
#include "ui/aemon_Shell.h"
