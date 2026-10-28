cmake_minimum_required(VERSION 3.22.1)
project(HengbandMobile C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUSE_ANDROID)

# 不足している xoshiro-cpp を取得
FetchContent_Declare(
    xoshiro_repo
    GIT_REPOSITORY https://github.com/Reputeless/Xoshiro-cpp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(xoshiro_repo)
include_directories(${xoshiro_repo_SOURCE_DIR})

# 変愚蛮怒のヘッダーパス
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# サブディレクトリをヘッダー検索に追加
file(GLOB_RECURSE ALL_SRC_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
foreach(DIR ${ALL_SRC_DIRS})
    if(IS_DIRECTORY ${DIR})
        include_directories(${DIR})
    endif()
endforeach()

# ソースコード一覧の取得
file(GLOB_RECURSE ALL_SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# テストコードの除外
file(GLOB_RECURSE TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/test/*")
if(TEST_FILES)
    list(REMOVE_ITEM ALL_SRC_FILES ${TEST_FILES})
endif()

# 共有ライブラリのビルド
add_library(hengband SHARED ${ALL_SRC_FILES})

# 依存ライブラリのリンク
target_link_libraries(hengband android log SDL2 SDL2_image SDL2_ttf)
