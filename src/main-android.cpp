#include <SDL.h>

// 変愚蛮怒本来のメイン関数（C++）
extern int main(int argc, char *argv[]);

// Android/SDL2が探しに来るC言語リンケージのエントリポイント
extern "C" int SDL_main(int argc, char *argv[]) {
    return main(argc, argv);
}
