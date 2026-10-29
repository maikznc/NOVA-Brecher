cmake_minimum_required(VERSION 3.22)

project(NovaBrecher VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal Binary (Apple Silicon + Intel) on macOS, unless set from outside
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- JUCE
# Either point JUCE_PATH at a local JUCE checkout, or it gets downloaded.
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------- Plugin
if(APPLE)
    set(BM_FORMATS VST3 AU)
    set(BM_COPY_DEFAULT ON)
else()
    set(BM_FORMATS VST3)
    set(BM_COPY_DEFAULT OFF)
endif()
option(BM_COPY_PLUGIN "Copy the plugin into the system plugin folder after building" ${BM_COPY_DEFAULT})

juce_add_plugin(NovaBrecher
    COMPANY_NAME            "NOVA"
    BUNDLE_ID               "com.nova.brecher"
    PLUGIN_MANUFACTURER_CODE Nova
    PLUGIN_CODE             Brch
    FORMATS                 ${BM_FORMATS}
    PRODUCT_NAME            "NOVA BRECHER"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         Fx Delay
    AU_MAIN_TYPE            kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${BM_COPY_PLUGIN})

set(BM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp)

target_sources(NovaBrecher PRIVATE ${BM_SOURCES})
target_include_directories(NovaBrecher PRIVATE Source)

set(BM_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(NovaBrecher PUBLIC ${BM_DEFINITIONS})

set(BM_MODULES
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics)

target_link_libraries(NovaBrecher
    PRIVATE ${BM_MODULES}
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- Tests (optional)
# Offline render + GUI snapshot. Not needed for normal use.
option(BM_BUILD_TESTS "Build the offline test tool" OFF)
if(BM_BUILD_TESTS)
    juce_add_console_app(NovaBrecherTests PRODUCT_NAME "NovaBrecherTests")
    target_sources(NovaBrecherTests PRIVATE ${BM_SOURCES} Tests/TestMain.cpp)
    target_include_directories(NovaBrecherTests PRIVATE Source)
    target_compile_definitions(NovaBrecherTests PRIVATE ${BM_DEFINITIONS} BM_TEST_BUILD=1
        JUCE_USE_XINPUT=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NovaBrecherTests PRIVATE ${BM_MODULES} juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
