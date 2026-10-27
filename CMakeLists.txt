cmake_minimum_required(VERSION 3.22.1)
project(HengbandMobile C)

add_definitions(-DUSE_ANDROID)

# 変愚蛮怒の本家が持っているインクルード構造をそのまま活かす
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB ALL_SRC_FILES "src/*.c")

# 他OS用のメイン描画ファイルを除外
list(REMOVE_ITEM ALL_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-win.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-mac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-x11.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-sdl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-gcu.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-cap.c"
)

add_library(hengband SHARED ${ALL_SRC_FILES})

target_link_libraries(hengband
    android
    log
    SDL2
    SDL2_image
    SDL2_ttf
)
