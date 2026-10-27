cmake_minimum_required(VERSION 3.22.1)

# C言語とC++の両方を有効化
project(HengbandMobile C CXX)

# 変愚蛮怒は現代的なC++20で記述されているため標準規格を指定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUSE_ANDROID)

# ヘッダー検索パス
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# ★修正：.c だけでなく .cpp もすべて再帰的に集める
file(GLOB_RECURSE ALL_SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ユニットテスト用のコード（src/test/配下）はビルド対象から除外
file(GLOB_RECURSE TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/test/*")
if(TEST_FILES)
    list(REMOVE_ITEM ALL_SRC_FILES ${TEST_FILES})
endif()

# Androidアプリ用の共有ライブラリ(libhengband.so)として設定
add_library(hengband SHARED ${ALL_SRC_FILES})

# Android OSの基本機能とSDL2ライブラリを紐付ける
target_link_libraries(hengband
    android
    log
    SDL2
    SDL2_image
    SDL2_ttf
)
