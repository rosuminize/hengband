cmake_minimum_required(VERSION 3.22.1)
project(HengbandMobile C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 種族名「ANDROID」とNDKマクロの衝突を回避
add_compile_options(-UANDROID)

# Android用マクロ & fmtライブラリのヘッダーオンリー有効化
add_definitions(-DUSE_ANDROID -DFMT_HEADER_ONLY)

# --- SDL2 本体のビルド組み込み ---
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SDL2 external/SDL2_build EXCLUDE_FROM_ALL)

# --- SDL2_image のビルド組み込み ---
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SDL2_image external/SDL2_image_build EXCLUDE_FROM_ALL)

# --- SDL2_ttf のビルド組み込み ---
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SDL2_ttf external/SDL2_ttf_build EXCLUDE_FROM_ALL)

# xoshiro-cpp の取得
FetchContent_Declare(
    xoshiro_repo
    GIT_REPOSITORY https://github.com/Reputeless/Xoshiro-cpp.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(xoshiro_repo)
include_directories(${xoshiro_repo_SOURCE_DIR})
include_directories(${xoshiro_repo_SOURCE_DIR}/include)

# 変愚蛮怒のヘッダーパス
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# サブディレクトリをヘッダー検索に追加
file(GLOB_RECURSE ALL_SRC_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
foreach(DIR ${ALL_SRC_DIRS})
    if(IS_DIRECTORY ${DIR})
        include_directories(${DIR})
    endif()
endforeach()

# ソースコード一覧の取得（.c, .cpp, .cc を対象に含める）
file(GLOB_RECURSE ALL_SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

# ユニットテストコードの除外
file(GLOB_RECURSE TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/test/*")
if(TEST_FILES)
    list(REMOVE_ITEM ALL_SRC_FILES ${TEST_FILES})
endif()

# 他プラットフォーム専用コード・外部依存（curl）のピンポイント除外
file(GLOB_RECURSE WIN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/main-win/*" "${CMAKE_CURRENT_SOURCE_DIR}/src/main-win.cpp")
file(GLOB_RECURSE NET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/net/*")

list(REMOVE_ITEM ALL_SRC_FILES
    ${WIN_FILES}
    ${NET_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-cap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-gcu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-x11.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-mac.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-sdl.cpp"
    # Linuxデスクトップ専用プレイヤー呼び出し（posix_spawnp依存）のみピンポイント除外
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-unix/unix-music.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main-unix/unix-sound.cpp"
)

# 共有ライブラリのビルド
add_library(hengband SHARED ${ALL_SRC_FILES})

# 依存ライブラリのリンク
target_link_libraries(hengband
    android
    log
    SDL2::SDL2
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
)
