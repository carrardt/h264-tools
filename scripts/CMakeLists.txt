configure_file(mkv2mkv.in mkv2mkv @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/mkv2mkv DESTINATION bin)

