cmake_minimum_required(VERSION 3.22)

project(ParametricEQ VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
#   PEQ_BUILD_PLUGIN  Build the JUCE VST3 plugin (needs JUCE; fetched automatically).
#   PEQ_BUILD_TESTS   Build the DSP unit tests (no dependencies).
#   PEQ_JUCE_DIR      Use a local JUCE checkout instead of downloading one.
#   PEQ_COPY_AFTER_BUILD  Copy the .vst3 into the system VST3 folder after
#                     building (needs an elevated prompt on Windows).
# ---------------------------------------------------------------------------
option(PEQ_BUILD_PLUGIN "Build the VST3 plugin" ON)
option(PEQ_BUILD_TESTS  "Build the DSP unit tests" ON)
option(PEQ_BUILD_GUI_TESTS "Build the headless GUI test (needs a display; development only)" OFF)
option(PEQ_COPY_AFTER_BUILD "Copy the VST3 to the system plugin folder after building" OFF)
set(PEQ_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(PEQ_JUCE_TAG "8.0.12" CACHE STRING "JUCE git tag to download when PEQ_JUCE_DIR is empty")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Core DSP library: plain C++17, no JUCE, so it can be unit-tested anywhere.
# ---------------------------------------------------------------------------
add_library(peq_dsp STATIC
    Source/DSP/Biquad.h
    Source/DSP/FilterDesign.h
    Source/DSP/FilterDesign.cpp
    Source/DSP/EQBand.h
    Source/DSP/EQBand.cpp
    Source/DSP/FilterEngine.h
    Source/DSP/FilterEngine.cpp
    Source/DSP/SampleFifo.h
    Source/DSP/Dynamics.h
    Source/DSP/AutoGain.h
    Source/DSP/AutoGain.cpp
    Source/DSP/SpectrumAnalyser.h
    Source/DSP/SpectrumAnalyser.cpp
)
target_include_directories(peq_dsp PUBLIC Source)
set_target_properties(peq_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(PEQ_BUILD_TESTS)
    enable_testing()
    add_executable(peq_tests
        Tests/TestFramework.h
        Tests/TestMain.cpp
        Tests/DSPTests.cpp
        Tests/EngineTests.cpp
        Tests/GraphTests.cpp
        Tests/AnalyserTests.cpp
        Tests/DynamicsTests.cpp
        Tests/MatchedTests.cpp
        Tests/AutoGainTests.cpp
    )
    find_package(Threads REQUIRED)
    target_link_libraries(peq_tests PRIVATE peq_dsp Threads::Threads)
    add_test(NAME peq_dsp_tests COMMAND peq_tests)
endif()

# ---------------------------------------------------------------------------
# VST3 plugin
# ---------------------------------------------------------------------------
if(PEQ_BUILD_PLUGIN)
    if(PEQ_JUCE_DIR)
        add_subdirectory(${PEQ_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${PEQ_JUCE_TAG}
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    add_subdirectory(Source)
endif()
