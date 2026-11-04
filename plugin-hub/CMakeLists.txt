cmake_minimum_required(VERSION 3.22)
project(PluginHub VERSION 1.0.1 LANGUAGES C CXX)
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
add_compile_definitions(NOMINMAX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)

juce_add_binary_data(HubAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(HubAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(HUB_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    HUB_VERSION_STRING="${PROJECT_VERSION}")

juce_add_gui_app(PluginHub
    PRODUCT_NAME "CMPLXTY Plugin Hub"
    COMPANY_NAME "CMPLXTY"
    BUNDLE_ID com.cmplxty.pluginhub
    ICON_BIG assets/icon.png
    VERSION ${PROJECT_VERSION})
target_sources(PluginHub PRIVATE src/Main.cpp src/HubCore.cpp)
target_compile_definitions(PluginHub PRIVATE ${HUB_DEFS} JUCE_APPLICATION_NAME_STRING="CMPLXTY Plugin Hub"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(PluginHub PRIVATE HubAssets juce::juce_gui_basics juce::juce_cryptography
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(WIN32)
    # one UAC prompt for the whole session: plug-ins go to Common Files\VST3 / CLAP for all users
    target_link_options(PluginHub PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

juce_add_console_app(PluginHubTest PRODUCT_NAME "PluginHubTest")
target_sources(PluginHubTest PRIVATE test/hub_test.cpp src/HubCore.cpp)
target_compile_definitions(PluginHubTest PRIVATE ${HUB_DEFS})
target_link_libraries(PluginHubTest PRIVATE juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
set_target_properties(PluginHubTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
