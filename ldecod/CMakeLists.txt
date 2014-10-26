file(GLOB LDECODSRCS "*.c")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -pedantic -fno-strict-aliasing -fsigned-char -Wall")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -march=native -mtune=native -fopenmp")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fopenmp")

add_executable(ldecod ${LDECODSRCS})
target_link_libraries(ldecod m)

install(TARGETS ldecod DESTINATION bin)
install(FILES decoder.cfg DESTINATION share)

