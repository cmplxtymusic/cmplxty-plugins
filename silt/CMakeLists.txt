cmake_minimum_required(VERSION 3.22)
project(Silt VERSION 1.0.0 LANGUAGES C CXX)
# Linux builds are for the tests only; the box sysroot has no libxi headers
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
add_compile_definitions(NOMINMAX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(SLT_VST3_AUTO_MANIFEST FALSE)
else()
    set(SLT_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Silt
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Silt"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE Silt
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Synth Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${SLT_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET Silt
    CLAP_ID "com.cmplxty.silt"
    CLAP_FEATURES instrument synthesizer sampler stereo)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(Silt_CLAP "CMPLXTY Silt" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(SLTAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(SLTAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Silt PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp)

target_compile_definitions(Silt PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Silt PRIVATE
    SLTAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(Silt PUBLIC SLT_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(SiltUpdateTest PRODUCT_NAME "SiltUpdateTest")
target_sources(SiltUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(SiltUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(SiltUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    SLT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(SiltUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  SiltScreenshot out.png [banner|menu|keys|drop|proc|...]
juce_add_console_app(SiltScreenshot PRODUCT_NAME "SiltScreenshot")
target_sources(SiltScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp)
target_include_directories(SiltScreenshot PRIVATE src)
target_link_libraries(SiltScreenshot PRIVATE SLTAssets juce::juce_audio_utils juce::juce_recommended_config_flags ${CMAKE_DL_LIBS})
target_compile_definitions(SiltScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 SLT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(SiltScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Engine tests (plain C++, no JUCE): root keys, tuning, grains, slices, sequencer, clicks, speed
add_executable(SiltDspTest test/dsp_test.cpp)
set_target_properties(SiltDspTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
