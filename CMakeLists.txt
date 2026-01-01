cmake_minimum_required (VERSION 3.12)
project(png-parser LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

project ("png-parser")

# Add source to this project's executable.
add_executable (png-parser "main.cpp")

target_link_libraries(png-parser PRIVATE SFML::Graphics)

target_compile_definitions(png-parser PRIVATE TEST_FILES_DIR="${PROJECT_SOURCE_DIR}/png-test-files")

set_property(TARGET png-parser PROPERTY CXX_STANDARD 23)
