cmake_minimum_required(VERSION 3.16)
project(AceSpeeder C)

set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(ENGINE_SRC
  src/engine/rmath.c
  src/engine/xfile.c
  src/engine/image.c
  src/engine/rm.c
  src/platform/mixer.c
  src/platform/linux/plat_linux.c
)

add_library(engine STATIC ${ENGINE_SRC})
target_include_directories(engine PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(engine PUBLIC SDL2::SDL2 OpenGL::GL m)
target_compile_options(engine PRIVATE -Wall -Wextra -Wno-unused-parameter)
# stb_vorbis is third-party code: don't drown our warnings in its own
set_source_files_properties(src/platform/mixer.c PROPERTIES COMPILE_OPTIONS "-w")

add_library(game STATIC src/game/gmath.c src/game/track.c)
target_link_libraries(game PUBLIC engine)
target_compile_options(game PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(viewer src/tools/viewer.c)
target_link_libraries(viewer engine)

add_executable(trackview src/tools/trackview.c)
target_link_libraries(trackview game)
