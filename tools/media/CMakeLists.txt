# Website media: clips rendered by the real engines. Off by default (option AEMON_MEDIA_TOOLS).
# AGPL-3.0-or-later.
juce_add_console_app(AemonClips PRODUCT_NAME "AemonClips")
target_sources(AemonClips PRIVATE Clips.cpp)
target_include_directories(AemonClips PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/utility/Source
    ${CMAKE_SOURCE_DIR}/plugins/eq/Source
    ${CMAKE_SOURCE_DIR}/plugins/compressor/Source
    ${CMAKE_SOURCE_DIR}/plugins/limiter/Source)
target_compile_definitions(AemonClips PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonClips PRIVATE juce::juce_audio_basics juce::juce_recommended_config_flags)
