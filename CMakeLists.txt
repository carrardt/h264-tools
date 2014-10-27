cmake_minimum_required (VERSION 2.6)
project (h264NaluAnalyzer)

find_package(ffmpeg 2.4 REQUIRED)

add_subdirectory(tools)
add_subdirectory(ldecod)
add_subdirectory(scripts)

