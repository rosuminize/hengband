cmake_minimum_required(VERSION 3.22.1)
project(HengbandMobile C CXX)

include(FetchContent)

# 変愚蛮怒が要求するC++20規格
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUSE_ANDROID)

# ★不足している xoshiro-cpp を自動取得してヘッダーパスに通す
FetchContent_Declare(
    xoshiro_repo
    GIT_REPOSITORY https://github.com/Reputeless/Xoshiro-cpp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(xoshiro_repo)
include_directories(${xoshiro_repo_SOURCE_DIR})

# 変愚蛮怒本体のヘッダー検索パス
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 全サブディレクトリもヘッダー検索に含める
file(GLOB_RECURSE ALL_SRC_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
foreach(DIR ${ALL_SRC_DIRS})
    if(IS_DIRECTORY ${DIR})
        include_directories(${DIR})
    endif()
endforeach()

# ソースコードを再帰取得
file(GLOB_RECURSE ALL_SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ユニットテストコードを除外
file(GLOB_RECURSE TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/test/*")
if(TEST_FILES)
    list(REMOVE_ITEM ALL_SRC_FILES ${TEST_FILES})
endif()

# Android用共有ライブラリ
add_library(hengband SHARED ${ALL_SRC_FILES})

# リンク設定
target_link_libraries(hengband
    android
    log
    SDL2
    SDL2_image
    SDL2_ttf
)
    SDL2_ttf
)
