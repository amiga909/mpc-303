cmake_minimum_required(VERSION 3.24)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(Acid3 VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

# Workaround for broken Command Line Tools installs whose toolchain ships an incomplete
# usr/include/c++/v1 that shadows the SDK's libc++ headers.
execute_process(COMMAND xcrun --find clang++ OUTPUT_VARIABLE _acid3_clang OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(_acid3_tc_root "${_acid3_clang}" DIRECTORY)
set(_acid3_sdk "${CMAKE_OSX_SYSROOT}")
if(APPLE AND NOT IS_DIRECTORY "${_acid3_sdk}")
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _acid3_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(APPLE AND EXISTS "${_acid3_tc_root}/../include/c++/v1" AND NOT EXISTS "${_acid3_tc_root}/../include/c++/v1/array"
   AND EXISTS "${_acid3_sdk}/usr/include/c++/v1/array")
    message(STATUS "Acid3: using SDK libc++ headers (toolchain copy is incomplete)")
    string(APPEND CMAKE_CXX_FLAGS " -nostdinc++ -isystem ${_acid3_sdk}/usr/include/c++/v1")
    string(APPEND CMAKE_OBJCXX_FLAGS " -nostdinc++ -isystem ${_acid3_sdk}/usr/include/c++/v1")
    # JUCE builds its juceaide helper in a nested configure step; it only sees the environment
    set(ENV{CPLUS_INCLUDE_PATH} "${_acid3_sdk}/usr/include/c++/v1")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ACID3_BUILD_PLUGIN "Build the VST3/Standalone plugin" ON)
option(ACID3_BUILD_TESTS "Build unit tests" ON)

# ---- core library (pure C++, no JUCE) ---------------------------------------------------
add_library(acid3_core STATIC
    src/dsp/Voice.cpp
    src/seq/Pattern.cpp
    src/seq/Sequencer.cpp
    src/seq/Engine.cpp
    src/plugin/Presets.cpp)
target_include_directories(acid3_core PUBLIC src)
target_compile_options(acid3_core PRIVATE -Wall -Wextra)
set_target_properties(acid3_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(acid3-render tools/render/main.cpp)
target_link_libraries(acid3-render PRIVATE acid3_core)

# ---- dependencies: a local checkout in extern/<name> if there is one, else downloaded (pinned) ----
include(FetchContent)
function(acid3_dependency name repo tag)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/${name}/CMakeLists.txt")
        add_subdirectory("extern/${name}" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW TRUE EXCLUDE_FROM_ALL)
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()

# ---- tests --------------------------------------------------------------------------------
if(ACID3_BUILD_TESTS)
    acid3_dependency(Catch2 https://github.com/catchorg/Catch2.git v3.16.1)
    add_executable(acid3_tests
        tests/TestOscillator.cpp
        tests/TestFilter.cpp
        tests/TestVoice.cpp
        tests/TestSequencer.cpp)
    target_link_libraries(acid3_tests PRIVATE acid3_core Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME acid3_tests COMMAND acid3_tests)
endif()

# ---- plugin -------------------------------------------------------------------------------
if(ACID3_BUILD_PLUGIN)
    acid3_dependency(JUCE https://github.com/juce-framework/JUCE.git 8.0.15)

    juce_add_plugin(Acid3
        COMPANY_NAME "Acid3"
        BUNDLE_ID "com.acid3.synth"
        PLUGIN_MANUFACTURER_CODE Acd3
        PLUGIN_CODE A303
        FORMATS VST3 Standalone
        PRODUCT_NAME "Acid3"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(Acid3 PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/Parameters.cpp)

    target_compile_definitions(Acid3 PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Acid3
        PRIVATE
            acid3_core
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # JUCE's VST3 manifest helper is a nested CMake project that only inherits this target's
    # include directories, so the libc++ workaround has to be visible there too
    if(DEFINED ENV{CPLUS_INCLUDE_PATH})
        target_include_directories(Acid3 BEFORE PRIVATE "$ENV{CPLUS_INCLUDE_PATH}")
    endif()

    # host-side smoke test that loads the built VST3 like a DAW would
    juce_add_console_app(acid3-hostcheck PRODUCT_NAME "acid3-hostcheck")
    target_sources(acid3-hostcheck PRIVATE tools/hostcheck/main.cpp)
    target_compile_definitions(acid3-hostcheck PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(acid3-hostcheck PRIVATE
        juce::juce_audio_processors juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_dependencies(acid3-hostcheck Acid3_VST3)
    # keep the host's inline JUCE symbols private so they can't be coalesced with the plugin's
    set_target_properties(acid3-hostcheck PROPERTIES CXX_VISIBILITY_PRESET hidden OBJCXX_VISIBILITY_PRESET hidden
                                                     VISIBILITY_INLINES_HIDDEN ON)
endif()
