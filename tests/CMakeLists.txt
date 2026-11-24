# Objective DSP tests. AGPL-3.0-or-later.
juce_add_console_app(AemonTests PRODUCT_NAME "AemonTests")
target_sources(AemonTests PRIVATE UtilityTests.cpp)
target_compile_definitions(AemonTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonTests PRIVATE AemonUtilityCode juce::juce_recommended_config_flags)

# EQ engine tests: plain C++, no JUCE.
add_executable(AemonEqEngineTests EqEngineTests.cpp)
target_include_directories(AemonEqEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/eq/Source)
target_compile_features(AemonEqEngineTests PRIVATE cxx_std_20)

add_executable(AemonLinearPhaseTests LinearPhaseTests.cpp)
target_include_directories(AemonLinearPhaseTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/eq/Source)
target_compile_features(AemonLinearPhaseTests PRIVATE cxx_std_20)

# EQ plugin-level tests and screenshots
juce_add_console_app(AemonEqTests PRODUCT_NAME "AemonEqTests")
target_sources(AemonEqTests PRIVATE EqPluginTests.cpp)
target_compile_definitions(AemonEqTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonEqTests PRIVATE AemonEqCode juce::juce_recommended_config_flags)

# Compressor engine tests: plain C++, no JUCE
add_executable(AemonCompEngineTests CompEngineTests.cpp)
target_include_directories(AemonCompEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/compressor/Source)
target_compile_features(AemonCompEngineTests PRIVATE cxx_std_20)

# Compressor plugin-level tests and screenshots
juce_add_console_app(AemonCompTests PRODUCT_NAME "AemonCompTests")
target_sources(AemonCompTests PRIVATE CompPluginTests.cpp)
target_compile_definitions(AemonCompTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonCompTests PRIVATE AemonCompCode juce::juce_recommended_config_flags)

# Limiter engine tests: plain C++, no JUCE
add_executable(AemonLimEngineTests LimEngineTests.cpp)
target_include_directories(AemonLimEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/limiter/Source)
target_compile_features(AemonLimEngineTests PRIVATE cxx_std_20)

# Limiter plugin-level tests and screenshots
juce_add_console_app(AemonLimTests PRODUCT_NAME "AemonLimTests")
target_sources(AemonLimTests PRIVATE LimPluginTests.cpp)
target_compile_definitions(AemonLimTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(AemonLimTests PRIVATE AemonLimCode juce::juce_recommended_config_flags)
