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

# Clipper, Saturation, De-esser, Imager, Multiband: engines (plain C++, no JUCE), then plugins and screenshots
foreach (pair "clipper Clip" "saturation Sat" "deesser Deess" "imager Imager" "multiband Mb")
    separate_arguments(pair)
    list(GET pair 0 dir)
    list(GET pair 1 name)
    add_executable(Aemon${name}EngineTests ${name}EngineTests.cpp)
    target_include_directories(Aemon${name}EngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/${dir}/Source ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_compile_features(Aemon${name}EngineTests PRIVATE cxx_std_20)
    juce_add_console_app(Aemon${name}Tests PRODUCT_NAME "Aemon${name}Tests")
    target_sources(Aemon${name}Tests PRIVATE ${name}PluginTests.cpp)
    target_compile_definitions(Aemon${name}Tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(Aemon${name}Tests PRIVATE Aemon${name}Code juce::juce_recommended_config_flags)
endforeach()

# Shared sound cores: plain C++, no JUCE
foreach (t Distortion Crossover)
    add_executable(Aemon${t}Tests ${t}Tests.cpp)
    target_include_directories(Aemon${t}Tests PRIVATE ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_compile_features(Aemon${t}Tests PRIVATE cxx_std_20)
endforeach()
