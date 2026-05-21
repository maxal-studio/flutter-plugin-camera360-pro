cmake_minimum_required(VERSION 3.21)
project(stitch_test)

set(CMAKE_CXX_STANDARD 17)

# ── OpenCV (Homebrew on Apple Silicon) ────────────────────────────────────────
# Try the Homebrew cmake config first; fall back to a plain find_package so
# the project also works when OpenCV is installed differently.
set(_BREW_OPENCV_CMAKE /opt/homebrew/opt/opencv/lib/cmake/opencv4)
if(EXISTS ${_BREW_OPENCV_CMAKE})
    set(OpenCV_DIR ${_BREW_OPENCV_CMAKE})
endif()
find_package(OpenCV REQUIRED)

message(STATUS "OpenCV version : ${OpenCV_VERSION}")
message(STATUS "OpenCV include : ${OpenCV_INCLUDE_DIRS}")

# ── Umbrella sources (same files used by the Flutter builds) ──────────────────
set(UMBRELLA ${CMAKE_CURRENT_SOURCE_DIR}/../src/umbrella/Stitcher)

set(UMBRELLA_SOURCES
    ${UMBRELLA}/Stitcher.cpp
    ${UMBRELLA}/PanoramicStitcher.cpp
    ${UMBRELLA}/PanoramaCrop.cpp
    ${UMBRELLA}/PanoramaFill.cpp
    ${UMBRELLA}/OpenCvMatcher.cpp
    ${UMBRELLA}/MatchFallback.cpp
    ${UMBRELLA}/ExternalFeatures.cpp
    ${UMBRELLA}/ProgressTracker.cpp
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(stitch_test
    main.cpp
    ${UMBRELLA_SOURCES}
)

target_include_directories(stitch_test PRIVATE
    ${UMBRELLA}             # resolves Stitcher.h, PanoramicStitcher.hpp, etc.
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(stitch_test PRIVATE ${OpenCV_LIBS})

message(STATUS "Build with:  cmake --build . --config Release -j\$(sysctl -n hw.logicalcpu)")
message(STATUS "Run with:    ./stitch_test")
