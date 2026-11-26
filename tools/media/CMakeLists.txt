# Website media: clips rendered by the real engines. Off by default (option AEMON_MEDIA_TOOLS).
# AGPL-3.0-or-later.
juce_add_console_app(AemonClips PRODUCT_NAME "AemonClips")
target_sources(AemonClips PRIVATE Clips.cpp)
target_include_directories(AemonClips PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/utility/Source
    ${CMAKE_SOURCE_DIR}/plugins/eq/Source
    ${CMAKE_SOURCE_DIR}/plugins/compressor/Source
    ${CMAKE_SOURCE_DIR}/plugins/limiter/Source
    ${CMAKE_SOURCE_DIR}/plugins/clipper/Source
    ${CMAKE_SOURCE_DIR}/plugins/saturation/Source
    ${CMAKE_SOURCE_DIR}/plugins/imager/Source
    ${CMAKE_SOURCE_DIR}/plugins/multiband/Source
    ${CMAKE_SOURCE_DIR}/plugins/transient/Source
    ${CMAKE_SOURCE_DIR}/plugins/reverb/Source
    ${CMAKE_SOURCE_DIR}/plugins/delay/Source
    ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
target_compile_definitions(AemonClips PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonClips PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
