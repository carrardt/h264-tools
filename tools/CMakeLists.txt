add_executable(naluparser naluparser.c)
add_executable(yuvsbspipe yuvsbspipe.c)

install(TARGETS naluparser yuvsbspipe DESTINATION bin)

