#include <SDL.h>
#include <unistd.h>
#include <stdlib.h>

// SDL.h の「#define main SDL_main」マクロを無効化し、無限再帰を防ぐ
#undef main

// 変愚蛮怒本体の main 関数（src/main.cpp で定義）
extern int main(int argc, char *argv[]);

// Android/SDL2 が最初に呼ぶ C リンケージのエントリポイント
extern "C" int SDL_main(int argc, char *argv[]) {
    // Android のアプリ内部ストレージへ移動
    const char *files_dir = getenv("FILES_DIR");
    if (files_dir) {
        chdir(files_dir);
    }

    // 変愚蛮怒を SDL2 端末（画面）モードで起動するための引数を組み立て
    // "-m" "sdl" または引数なしで main に渡す
    char arg0[] = "hengband";
    char arg1[] = "-msdl";
    char *custom_argv[] = { arg0, arg1, nullptr };
    int custom_argc = 2;

    return main(custom_argc, custom_argv);
}
