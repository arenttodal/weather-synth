cmake_minimum_required(VERSION 3.22)
project(Atmospheric VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: CI downloads a pinned release; locally an installed JUCE (e.g. Ubuntu's
# juce-modules-source) is used when available.
option(ATMOS_FETCH_JUCE "Download JUCE instead of using an installed copy" OFF)
set(ATMOS_JUCE_TAG "8.0.8" CACHE STRING "JUCE release to download")
if(ATMOS_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${ATMOS_JUCE_TAG} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

# The weather relay the plugin talks to. Edit relay-url.txt (one line) or pass
# -DATMOS_RELAY_URL=...; users can also override it in settings.json.
set(ATMOS_RELAY_URL "" CACHE STRING "Base URL of the Atmospheric weather relay")
if(ATMOS_RELAY_URL STREQUAL "" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/relay-url.txt")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/relay-url.txt" ATMOS_RELAY_URL)
    string(STRIP "${ATMOS_RELAY_URL}" ATMOS_RELAY_URL)
endif()
message(STATUS "Atmospheric relay: '${ATMOS_RELAY_URL}'")

set(ATMOS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ATMOS_FORMATS AU)
endif()

juce_add_plugin(Atmospheric
    COMPANY_NAME "Atmospheric"
    BUNDLE_ID "app.atmospheric.synth"
    PLUGIN_MANUFACTURER_CODE Atmo
    PLUGIN_CODE Atm1
    FORMATS ${ATMOS_FORMATS}
    PRODUCT_NAME "Atmospheric"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED FALSE
    NEEDS_CURL TRUE)

juce_add_binary_data(AtmosData SOURCES Resources/land110m.bin)
set_target_properties(AtmosData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ATMOS_CORE_SOURCES
    Source/ClimateMapper.cpp
    Source/Astro.cpp
    Source/Wavetable.cpp
    Source/Engine.cpp
    Source/Day.cpp
    Source/Storage.cpp
    Source/WeatherClient.cpp
    Source/Globe.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(Atmospheric PRIVATE ${ATMOS_CORE_SOURCES})

set(ATMOS_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    ATMOS_RELAY_URL="${ATMOS_RELAY_URL}"
    ATMOS_VERSION_STRING="${PROJECT_VERSION}")

target_compile_definitions(Atmospheric PUBLIC ${ATMOS_DEFINES})

set(ATMOS_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics)

target_link_libraries(Atmospheric
    PRIVATE AtmosData ${ATMOS_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- Tests: mapping parity with the lab, astronomy, storage, state, offline renders
option(ATMOS_BUILD_TESTS "Build the test runner" ON)
if(ATMOS_BUILD_TESTS)
    juce_add_console_app(AtmosTests PRODUCT_NAME "AtmosTests" NEEDS_CURL TRUE)
    target_sources(AtmosTests PRIVATE tests/Tests.cpp ${ATMOS_CORE_SOURCES})
    target_compile_definitions(AtmosTests PRIVATE ${ATMOS_DEFINES}
        JucePlugin_Name="Atmospheric" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0 JucePlugin_EditorRequiresKeyboardFocus=1
        ATMOS_TEST_VECTORS="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors.json")
    target_link_libraries(AtmosTests PRIVATE AtmosData ${ATMOS_MODULES}
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---- Loads the built plugin like a DAW would (see tests/HostCheck.cpp)
if(ATMOS_BUILD_TESTS)
    juce_add_console_app(AtmosHostCheck PRODUCT_NAME "AtmosHostCheck" PLUGINHOST_AU TRUE)
    target_sources(AtmosHostCheck PRIVATE tests/HostCheck.cpp)
    target_compile_definitions(AtmosHostCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AtmosHostCheck PRIVATE juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
