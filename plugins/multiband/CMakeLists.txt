# Aemon Multiband. AGPL-3.0-or-later.
add_library(AemonMbCode INTERFACE)
target_sources(AemonMbCode INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/MbProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/MbEditor.cpp)
target_include_directories(AemonMbCode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
target_compile_definitions(AemonMbCode INTERFACE
    AEMON_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AemonMbCode INTERFACE
    aemon_core
    AemonFonts
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra)

juce_add_plugin(AemonMultiband
    PRODUCT_NAME "Aemon Multiband"
    COMPANY_NAME "Aemon"
    COMPANY_WEBSITE "https://github.com/aemon-audio/aemon"
    BUNDLE_ID org.aemon.multiband
    PLUGIN_MANUFACTURER_CODE Aemn
    PLUGIN_CODE AeMb
    VERSION ${PROJECT_VERSION}
    FORMATS ${AEMON_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_link_libraries(AemonMultiband
    PRIVATE
        AemonMbCode
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
