#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>

// 変愚蛮怒の端末・基本型ヘッダー
#include "term/gameterm.h"
#include "term/term-color-types.h"
#include "system/angband.h"

#ifdef main
#undef main
#endif

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int main(int argc, char *argv[]);

// --- SDL2 描画用グローバルリソース ---
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static TTF_Font *g_font = nullptr;
static int g_cell_w = 12;
static int g_cell_h = 24;
static term_type g_term; // ★ term_data ではなく正規の term_type を使用

// 標準出力ログ化
static void start_logger_thread() {
    int pfd[2];
    if (pipe(pfd) == -1) return;

    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    std::thread([read_fd = pfd[0]]() {
        char buf[1024];
        std::string line;
        ssize_t r;
        while ((r = read(read_fd, buf, sizeof(buf) - 1)) > 0) {
            buf[r] = '\0';
            line += buf;
            size_t pos;
            while ((pos = line.find('\n')) != std::string::npos) {
                std::string msg = line.substr(0, pos);
                LOGI("[Core] %s", msg.c_str());
                line.erase(0, pos + 1);
            }
        }
    }).detach();
}

// --- 変愚蛮怒 Term コールバック群 ---

static void Term_init_android(term_type *t) {
    (void)t;
    LOGI("Term_init_android called");
}

static void Term_nuke_android(term_type *t) {
    (void)t;
    LOGI("Term_nuke_android called");
}

static errr Term_user_android(int n) {
    (void)n;
    return 0;
}

static errr Term_xtra_android(int n, int v) {
    (void)v;
    switch (n) {
    case TERM_XTRA_CLEAR:
        if (g_renderer) {
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderClear(g_renderer);
        }
        return 0;
    case TERM_XTRA_FRESH:
        if (g_renderer) {
            SDL_RenderPresent(g_renderer);
        }
        return 0;
    case TERM_XTRA_EVENT:
    case TERM_XTRA_FLUSH:
    case TERM_XTRA_BORED:
        return 0;
    default:
        return 1;
    }
}

static errr Term_curs_android(int x, int y) {
    (void)x; (void)y;
    return 0;
}

static errr Term_bigcurs_android(int x, int y) {
    (void)x; (void)y;
    return 0;
}

static errr Term_wipe_android(int x, int y, int n) {
    if (!g_renderer) return 0;
    SDL_Rect r = { x * g_cell_w, y * g_cell_h, n * g_cell_w, g_cell_h };
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &r);
    return 0;
}

static errr Term_text_android(int x, int y, int n, byte a, concptr s) {
    (void)a;
    if (!g_renderer || !g_font || !s) return 0;

    Term_wipe_android(x, y, n);

    std::string text(s, n);
    SDL_Color fg = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, text.c_str(), fg);
    if (!surf) return 0;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    if (tex) {
        SDL_Rect dst = { x * g_cell_w, y * g_cell_h, surf->w, surf->h };
        SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    return 0;
}

// --- Android ディスプレイモジュールの初期化 ---

extern "C" errr init_android(int argc, char **argv) {
    (void)argc; (void)argv;
    LOGI("=== init_android() called ===");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    if (TTF_Init() < 0) {
        LOGE("TTF_Init failed: %s", TTF_GetError());
    }

    g_window = SDL_CreateWindow("Hengband Mobile",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                1080, 2058,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        LOGE("Window creation failed: %s", SDL_GetError());
        return -1;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        LOGE("Renderer creation failed: %s", SDL_GetError());
        return -1;
    }

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";
    
    std::vector<std::string> font_candidates = {
        files_dir + "/lib/xtra/font/NotoSansCJKjp-Regular.otf",
        files_dir + "/lib/xtra/font/NotoSansJP-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/Roboto-Regular.ttf"
    };

    for (const auto &path : font_candidates) {
        g_font = TTF_OpenFont(path.c_str(), 18);
        if (g_font) {
            LOGI("Loaded font from: %s", path.c_str());
            break;
        }
    }

    if (!g_font) {
        LOGE("WARNING: No font loaded! Text will not render.");
    }

    // 変愚蛮怒の端末初期化 (80x24)
    term_type *t = &g_term;
    term_init(t, 80, 24, 256);

    t->init_hook = Term_init_android;
    t->nuke_hook = Term_nuke_android;
    t->user_hook = Term_user_android;
    t->xtra_hook = Term_xtra_android;
    t->curs_hook = Term_curs_android;
    t->bigcurs_hook = Term_bigcurs_android;
    t->wipe_hook = Term_wipe_android;
    t->text_hook = Term_text_android;

    term_activate(t);
    angband_terms[0] = t;

    LOGI("init_android succeeded! Term activated.");
    return 0;
}

extern "C" int SDL_main(int argc, char *argv[]) {
    start_logger_thread();
    LOGI("=== Hengband Mobile Starting ===");

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";

    if (chdir(files_dir.c_str()) != 0) {
        LOGE("Failed to chdir to: %s", files_dir.c_str());
    }

    std::string lib_dir = files_dir + "/lib/";
    std::string user_dir = files_dir + "/user/";

    setenv("HOME", files_dir.c_str(), 1);
    setenv("ANGBAND_PATH", lib_dir.c_str(), 1);

    std::string arg_mod = "-mandroid";
    std::string arg_user = "-duser=" + user_dir;

    std::vector<char*> args;
    args.push_back(strdup("hengband"));
    args.push_back(strdup(arg_mod.c_str()));
    args.push_back(strdup(arg_user.c_str()));
    args.push_back(nullptr);

    LOGI("Calling REAL Hengband main() with: %s %s", args[1], args[2]);

    int result = main(static_cast<int>(args.size() - 1), args.data());

    LOGI("Hengband main() finished with code: %d", result);

    for (char* arg : args) {
        if (arg) free(arg);
    }

    return result;
}
