cmake_minimum_required(VERSION 3.15)
project(PyLine2 VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Local JUCE path
add_subdirectory(/Users/md/JUCE JUCE)

juce_add_gui_app(PyLine2
    PRODUCT_NAME "PyLine 2.0"
    COMPANY_NAME "Local"
    ICON_BIG "${CMAKE_CURRENT_LIST_DIR}/assets/pyl2_icon_1024.png"
)

juce_generate_juce_header(PyLine2)

target_sources(PyLine2
    PRIVATE
        Source/Main.cpp
        Source/TimelineModel.cpp
        Source/TimelineModel.h
        Source/PythonRenderer.cpp
        Source/PythonRenderer.h
)

# Basic JUCE modules

target_link_libraries(PyLine2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Python executable path (override in CMake cache if needed)
set(PYTHON_EXECUTABLE "/usr/bin/python3" CACHE STRING "Path to python3")

target_compile_definitions(PyLine2
    PRIVATE
        PYTHON_EXECUTABLE_PATH=\"${PYTHON_EXECUTABLE}\"
)
