cmake_minimum_required(VERSION 3.22)
project(MonoFET VERSION 1.0.0 LANGUAGES C CXX)

# JUCE fixado numa versao. Para usar uma copia local: -DFETCHCONTENT_SOURCE_DIR_JUCE=/caminho/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MonoFET
    COMPANY_NAME             "Cleber"
    PLUGIN_MANUFACTURER_CODE Cldr
    PLUGIN_CODE              Mfet
    FORMATS                  AU VST3
    PRODUCT_NAME             "MonoFET"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD  TRUE
    NEEDS_WEB_BROWSER        FALSE
    NEEDS_CURL               FALSE)

target_sources(MonoFET PRIVATE Source/PluginProcessor.cpp)
target_include_directories(MonoFET PRIVATE Source)
target_compile_definitions(MonoFET PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MonoFET
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Testes do DSP (nao dependem do JUCE)
option(MONOFET_BUILD_TESTS "Compila os testes do DSP" ON)
if(MONOFET_BUILD_TESTS)
    enable_testing()
    add_executable(dsp_test Tests/dsp_test.cpp)
    target_compile_features(dsp_test PRIVATE cxx_std_17)
    add_test(NAME dsp_test COMMAND dsp_test)
endif()
