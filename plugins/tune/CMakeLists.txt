# Aemon Tune. AGPL-3.0-or-later.
add_library(AemonTuneCode INTERFACE)
target_sources(AemonTuneCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/TuneProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/TuneEditor.cpp)
target_include_directories(AemonTuneCode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
target_compile_definitions(AemonTuneCode INTERFACE
    AEMON_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AemonTuneCode INTERFACE
    aemon_core
    AemonFonts
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra)

juce_add_plugin(AemonTune
    PRODUCT_NAME "Aemon Tune"
    COMPANY_NAME "Aemon"
    COMPANY_WEBSITE "https://github.com/aemon-audio/aemon"
    BUNDLE_ID org.aemon.tune
    PLUGIN_MANUFACTURER_CODE Aemn
    PLUGIN_CODE AeTn
    VERSION ${PROJECT_VERSION}
    FORMATS ${AEMON_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_link_libraries(AemonTune
    PRIVATE
        AemonTuneCode
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
