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
  src/engine/xfile.c src/engine/font.c src/engine/sprite.c
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

set(GAME_SRC src/game/gmath.c src/game/track.c src/game/machine.c src/game/camera.c src/game/effects.c src/game/hud.c src/game/race.c src/game/save.c src/game/replay.c)
# The original's physics ran in single precision: no fused/contracted operations.
set(GAME_FLAGS -Wall -Wextra -Wno-unused-parameter -ffp-contract=off)

add_library(game STATIC ${GAME_SRC} src/game/sound.c src/game/fx.c src/game/scene.c src/game/course_objects.c)
target_link_libraries(game PUBLIC engine)
target_compile_options(game PRIVATE ${GAME_FLAGS})

# Game logic as a shared library for the differential tests in re/scripts.
add_library(gametest SHARED ${GAME_SRC} src/tools/testlib.c src/engine/rmath.c)
target_compile_options(gametest PRIVATE ${GAME_FLAGS} -DGAME_TESTLIB)
target_link_libraries(gametest PRIVATE m)

add_executable(acespeeder src/main.c)
target_link_libraries(acespeeder game)
target_compile_options(acespeeder PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(viewer src/tools/viewer.c)
target_link_libraries(viewer engine)

add_executable(trackview src/tools/trackview.c)
target_link_libraries(trackview game)
