cmake_minimum_required(VERSION 3.22)
project(SonicDAW VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # нужно для clangd в VS Code

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4                # проверь актуальный тег на github.com/juce-framework/JUCE/releases
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(SonicDAW
    PRODUCT_NAME      "SonicDAW"
    COMPANY_NAME      "Sonic"
    BUNDLE_ID         com.sonic.sonicdaw
    NEEDS_MIDI_INPUT  TRUE
    NEEDS_MIDI_OUTPUT TRUE)

juce_generate_juce_header(SonicDAW)

target_sources(SonicDAW PRIVATE
    src/main.cpp
    src/app/MainComponent.cpp)

target_include_directories(SonicDAW PRIVATE src)

target_compile_definitions(SonicDAW PRIVATE
    JUCE_WEB_BROWSER=0      # убирает тяжёлые зависимости на Linux
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SonicDAW PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)