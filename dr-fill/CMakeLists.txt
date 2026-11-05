cmake_minimum_required(VERSION 3.22)
project(DrFill VERSION 1.0.1 LANGUAGES C CXX)
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
    set(DRF_VST3_AUTO_MANIFEST FALSE)
else()
    set(DRF_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(DrFill
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Dr Fill"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE DrFl
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Drum
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${DRF_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET DrFill
    CLAP_ID "com.cmplxty.dr-fill"
    CLAP_FEATURES instrument drum-machine stereo mono)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(DrFill_CLAP "CMPLXTY Dr Fill" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(DRFAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(DRFAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(DrFill PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp)

target_compile_definitions(DrFill PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(DrFill PRIVATE
    DRFAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(DrFill PUBLIC DRF_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(DrFillUpdateTest PRODUCT_NAME "DrFillUpdateTest")
target_sources(DrFillUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(DrFillUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(DrFillUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    DRF_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(DrFillUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  DrFillScreenshot out.png [banner|menu|keys|drop|proc|...]
juce_add_console_app(DrFillScreenshot PRODUCT_NAME "DrFillScreenshot")
target_sources(DrFillScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp)
target_include_directories(DrFillScreenshot PRIVATE src)
target_link_libraries(DrFillScreenshot PRIVATE DRFAssets juce::juce_audio_utils juce::juce_recommended_config_flags ${CMAKE_DL_LIBS})
target_compile_definitions(DrFillScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 DRF_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(DrFillScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Engine tests (plain C++, no JUCE): onsets, tempo, fills, clicks, speed
add_executable(DrFillDspTest test/dsp_test.cpp)
set_target_properties(DrFillDspTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
