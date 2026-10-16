# Android has no system copies of these, so they are built from source.
include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Config mode first, so the OVERRIDE_FIND_PACKAGE redirects win over the Find modules in CMake/ and in vorbis itself.
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

FetchContent_Declare(Ogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.5 GIT_SHALLOW TRUE
                     OVERRIDE_FIND_PACKAGE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Ogg)

FetchContent_Declare(Opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.5.2 GIT_SHALLOW TRUE
                     OVERRIDE_FIND_PACKAGE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Opus)

FetchContent_Declare(Vorbis GIT_REPOSITORY https://github.com/xiph/vorbis.git GIT_TAG v1.3.7 GIT_SHALLOW TRUE
                     OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(Vorbis)

# opusfile ships no CMake project, so only the sources soh needs are compiled (no HTTP streaming).
FetchContent_Declare(opusfile GIT_REPOSITORY https://github.com/xiph/opusfile.git GIT_TAG v0.12 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(opusfile)
add_library(opusfile STATIC
    ${opusfile_SOURCE_DIR}/src/info.c
    ${opusfile_SOURCE_DIR}/src/internal.c
    ${opusfile_SOURCE_DIR}/src/opusfile.c
    ${opusfile_SOURCE_DIR}/src/picture.c
    ${opusfile_SOURCE_DIR}/src/stream.c
)
target_include_directories(opusfile PUBLIC ${opusfile_SOURCE_DIR}/include)
target_link_libraries(opusfile PUBLIC Ogg::ogg Opus::opus)
add_library(OpusFile::opusfile ALIAS opusfile)

# SDL_net's own CMake looks SDL2 up through a helper that can't see the copy libultraship builds, so
# only its C sources are compiled. SOURCE_SUBDIR points at nothing so MakeAvailable just downloads it.
FetchContent_Declare(SDL2_net GIT_REPOSITORY https://github.com/libsdl-org/SDL_net.git GIT_TAG release-2.2.0
                     GIT_SHALLOW TRUE SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(SDL2_net)
# soh includes <SDL2/SDL_net.h>
file(COPY ${sdl2_net_SOURCE_DIR}/SDL_net.h DESTINATION ${CMAKE_BINARY_DIR}/sdl2_net_include/SDL2)
add_library(SDL2_net STATIC
    ${sdl2_net_SOURCE_DIR}/SDLnet.c
    ${sdl2_net_SOURCE_DIR}/SDLnetTCP.c
    ${sdl2_net_SOURCE_DIR}/SDLnetUDP.c
    ${sdl2_net_SOURCE_DIR}/SDLnetselect.c
)
target_include_directories(SDL2_net PUBLIC ${CMAKE_BINARY_DIR}/sdl2_net_include ${CMAKE_BINARY_DIR}/sdl2_net_include/SDL2)
target_include_directories(SDL2_net PRIVATE ${sdl2_net_SOURCE_DIR})
foreach(_sdl_target SDL2::SDL2 SDL2)
    if(TARGET ${_sdl_target})
        target_link_libraries(SDL2_net PUBLIC ${_sdl_target})
        break()
    endif()
endforeach()
add_library(SDL2_net::SDL2_net ALIAS SDL2_net)

# LUS may already pull libzip in for its own use.
if(NOT TARGET libzip::zip)
    set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libzip GIT_REPOSITORY https://github.com/nih-at/libzip.git GIT_TAG v1.11.4
                         GIT_SHALLOW TRUE OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(libzip)
endif()
