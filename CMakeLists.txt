cmake_minimum_required(VERSION 3.22.1)
project(HengbandMobile C)

# Android用のマクロ定義
add_definitions(-DUSE_ANDROID)

# 変愚蛮怒のヘッダーファイルの検索パスを通す
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# ★修正：srcフォルダの中にあるすべてのC言語ファイルを正しく取得する
file(GLOB ALL_SRC_FILES "src/*.c")

# Androidビルドでエラーの原因になる、他OS用の描画ファイルをリストから除外
list(REMOVE_ITEM ALL_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-win.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-mac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-x11.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-sdl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-gcu.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-cap.c"
)

# Androidアプリは共有ライブラリ(libhengband.so)としてビルドする
add_library(hengband SHARED ${ALL_SRC_FILES})

# Android OSの基本機能と、SDL2ライブラリを紐付ける
target_link_libraries(hengband
    android
    log
    SDL2
    SDL2_image
    SDL2_ttf
)
