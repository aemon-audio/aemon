# Objective DSP tests. AGPL-3.0-or-later.

# The JUCE modules, compiled once for all plugin-level test programs instead of once per program.
add_library(AemonTestJuce STATIC)
target_link_libraries(AemonTestJuce
    PRIVATE aemon_core AemonFonts juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags)
target_compile_definitions(AemonTestJuce
    PUBLIC AEMON_VERSION="${PROJECT_VERSION}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 JUCE_STANDALONE_APPLICATION=1
    INTERFACE $<TARGET_PROPERTY:AemonTestJuce,COMPILE_DEFINITIONS>)
target_include_directories(AemonTestJuce INTERFACE $<TARGET_PROPERTY:AemonTestJuce,INCLUDE_DIRECTORIES>)

# A test program: its own file and the plugin's sources, on the modules above.
function (aemon_plugin_test target code file)
    juce_add_console_app(${target} PRODUCT_NAME "${target}")
    get_target_property(sources ${code} INTERFACE_SOURCES)
    get_target_property(includes ${code} INTERFACE_INCLUDE_DIRECTORIES)
    target_sources(${target} PRIVATE ${file} ${sources})
    target_include_directories(${target} PRIVATE ${includes} ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_link_libraries(${target} PRIVATE AemonTestJuce AemonFonts)
endfunction()

aemon_plugin_test(AemonTests AemonUtilityCode UtilityTests.cpp)

# EQ engine tests: plain C++, no JUCE.
add_executable(AemonEqEngineTests EqEngineTests.cpp)
target_include_directories(AemonEqEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/eq/Source)
target_compile_features(AemonEqEngineTests PRIVATE cxx_std_20)

add_executable(AemonLinearPhaseTests LinearPhaseTests.cpp)
target_include_directories(AemonLinearPhaseTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/eq/Source)
target_compile_features(AemonLinearPhaseTests PRIVATE cxx_std_20)

# EQ plugin-level tests and screenshots
aemon_plugin_test(AemonEqTests AemonEqCode EqPluginTests.cpp)

# Compressor engine tests: plain C++, no JUCE
add_executable(AemonCompEngineTests CompEngineTests.cpp)
target_include_directories(AemonCompEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/compressor/Source)
target_compile_features(AemonCompEngineTests PRIVATE cxx_std_20)

# Compressor plugin-level tests and screenshots
aemon_plugin_test(AemonCompTests AemonCompCode CompPluginTests.cpp)

# Limiter engine tests: plain C++, no JUCE
add_executable(AemonLimEngineTests LimEngineTests.cpp)
target_include_directories(AemonLimEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/limiter/Source)
target_compile_features(AemonLimEngineTests PRIVATE cxx_std_20)

# Limiter plugin-level tests and screenshots
aemon_plugin_test(AemonLimTests AemonLimCode LimPluginTests.cpp)

# Clipper, Saturation, De-esser, Imager, Multiband: engines (plain C++, no JUCE), then plugins and screenshots
foreach (pair "clipper Clip" "saturation Sat" "deesser Deess" "imager Imager" "multiband Mb" "rider Rider" "transient Trans" "lowend Low" "reverb Rev" "delay Del")
    separate_arguments(pair)
    list(GET pair 0 dir)
    list(GET pair 1 name)
    add_executable(Aemon${name}EngineTests ${name}EngineTests.cpp)
    target_include_directories(Aemon${name}EngineTests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/${dir}/Source ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_compile_features(Aemon${name}EngineTests PRIVATE cxx_std_20)
    aemon_plugin_test(Aemon${name}Tests Aemon${name}Code ${name}PluginTests.cpp)
endforeach()

# Tune: hearing, pitch change, the whole engine (plain C++), then the plugin
foreach (t Engine Shift Note)
    add_executable(AemonTune${t}Tests Tune${t}Tests.cpp)
    target_include_directories(AemonTune${t}Tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/tune/Source ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_compile_features(AemonTune${t}Tests PRIVATE cxx_std_20)
endforeach()
aemon_plugin_test(AemonTuneTests AemonTuneCode TunePluginTests.cpp)

# Shared sound cores: plain C++, no JUCE
foreach (t Distortion Crossover Percept)
    add_executable(Aemon${t}Tests ${t}Tests.cpp)
    target_include_directories(Aemon${t}Tests PRIVATE ${CMAKE_SOURCE_DIR}/shared/aemon_core/dsp)
    target_compile_features(Aemon${t}Tests PRIVATE cxx_std_20)
endforeach()
