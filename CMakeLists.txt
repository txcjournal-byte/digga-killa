cmake_minimum_required(VERSION 3.22)

project(DiggaKilla VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# JUCE
#   - default: fetched from GitHub at a pinned tag
#   - offline / faster: cmake -DDIGGA_JUCE_PATH=/path/to/JUCE ..
# ------------------------------------------------------------------------------
set(DIGGA_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(DIGGA_JUCE_PATH)
    add_subdirectory(${DIGGA_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------
juce_add_plugin(DiggaKilla
    PRODUCT_NAME                "Digga Killa"
    COMPANY_NAME                "TrapVST"
    BUNDLE_ID                   com.trapvst.diggakilla
    PLUGIN_MANUFACTURER_CODE    Trvs
    PLUGIN_CODE                 Dgkl
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Sampler
    FORMATS                     VST3 Standalone)

juce_add_binary_data(DiggaKillaAssets
    SOURCES
        assets/fonts/ArchivoBlack-Regular.ttf
        assets/fonts/BarlowCondensed-SemiBold.ttf
        assets/fonts/BarlowCondensed-Bold.ttf
        assets/fonts/CourierPrime-Regular.ttf
        assets/fonts/CourierPrime-Bold.ttf
        assets/skin/background.png
        assets/skin/drop_sample.png
        assets/skin/kill_stamp.png)

target_sources(DiggaKilla PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/core/JobQueue.cpp
    source/core/SampleStore.cpp
    source/playback/SamplePlayer.cpp
    source/ui/Theme.cpp
    source/ui/DiggaLookAndFeel.cpp
    source/ui/TempoDisplay.cpp
    source/ui/RecordLabel.cpp
    source/ui/TrackRow.cpp
    source/ui/TrackColumn.cpp
    source/ui/FxPanel.cpp
    source/ui/MainView.cpp)

target_include_directories(DiggaKilla PRIVATE source)

target_compile_definitions(DiggaKilla PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(DiggaKilla
    PRIVATE
        DiggaKillaAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Dev tool: headless UI snapshot (cmake -DDIGGA_BUILD_TOOLS=ON)
# ------------------------------------------------------------------------------
option(DIGGA_BUILD_TOOLS "Build developer tools" OFF)

if(DIGGA_BUILD_TOOLS)
    juce_add_console_app(DiggaKillaSnapshot PRODUCT_NAME "DiggaKillaSnapshot")
    target_sources(DiggaKillaSnapshot PRIVATE tools/Snapshot.cpp)
    # reuse the plugin's compiled JUCE modules instead of building them twice
    target_include_directories(DiggaKillaSnapshot PRIVATE source
        $<TARGET_PROPERTY:DiggaKilla,INCLUDE_DIRECTORIES>)
    target_compile_definitions(DiggaKillaSnapshot PRIVATE
        $<TARGET_PROPERTY:DiggaKilla,COMPILE_DEFINITIONS>)
    target_link_libraries(DiggaKillaSnapshot PRIVATE DiggaKilla)
endif()
