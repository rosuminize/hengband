#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <clocale>
#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <string>
#include <thread>
#include <mutex>

// 変愚蛮怒の端末・基本型ヘッダー
#include "term/gameterm.h"
#include "term/z-term.h"
#include "term/term-color-types.h"
#include "system/angband.h"

#ifdef main
#undef main
#endif

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int main(int argc, char *argv[]);

// --- SDL2 描画用リソース ---
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static TTF_Font *g_font_game = nullptr;
static TTF_Font *g_font_msg = nullptr;
static TTF_Font *g_font_btn = nullptr;

// 画面分割レイアウト座標
static SDL_Rect g_rect_game; // 上部 55%
static SDL_Rect g_rect_msg;  // 中間 10%
static SDL_Rect g_rect_pad;  // 下部 35%

static int g_cell_w = 12;
static int g_cell_h = 24;
static term_type g_term;

// メッセージログバッファ（直近のメッセージを保持）
static std::deque<std::string> g_message_history;
static std::mutex g_msg_mutex;
static std::string g_current_menu_text = "";
static bool g_in_menu_mode = false;

// 変愚蛮怒のキーバッファへの注入
static inline void send_key(int ch) {
    term_key_push(ch);
}

// --- バーチャルキーパッド定義 ---
struct VKey {
    SDL_Rect rect;
    const char *label;
    char key;
    bool is_special;
    int special_id;
};

static std::vector<VKey> g_vkeys;

// 長押しリピート管理
static SDL_FingerID g_holding_finger = -1;
static char g_holding_key = 0;
static Uint32 g_hold_start_time = 0;
static Uint32 g_last_repeat_time = 0;

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

// 55% / 10% / 35% の画面分割レイアウト計算
static void setup_layout(int win_w, int win_h) {
    // 1. 上部 55%: メインゲーム画面
    g_rect_game.x = 0;
    g_rect_game.y = 0;
    g_rect_game.w = win_w;
    g_rect_game.h = static_cast<int>(win_h * 0.55);

    // 80文字×22行程度がすっぽり収まるセルサイズ
    g_cell_w = win_w / 80;
    if (g_cell_w < 12) g_cell_w = 12;
    g_cell_h = g_rect_game.h / 24;

    // 2. 中間 10%: メッセージ＆メニュー共用エリア
    g_rect_msg.x = 8;
    g_rect_msg.y = g_rect_game.h + 4;
    g_rect_msg.w = win_w - 16;
    g_rect_msg.h = static_cast<int>(win_h * 0.10) - 8;

    // 3. 下部 35%: バーチャルキーパッド
    g_rect_pad.x = 10;
    g_rect_pad.y = g_rect_msg.y + g_rect_msg.h + 8;
    g_rect_pad.w = win_w - 20;
    g_rect_pad.h = win_h - g_rect_pad.y - 10;

    // キーパッドボタン配置
    g_vkeys.clear();
    int dpad_w = static_cast<int>(g_rect_pad.w * 0.60);
    int btn_w = dpad_w / 3 - 6;
    int btn_h = g_rect_pad.h / 3 - 6;

    const char *dpad_labels[3][3] = {
        { "7", "8", "9" },
        { "4", "5", "6" },
        { "1", "2", "3" }
    };

    char dpad_keys[3][3] = {
        { '7', '8', '9' },
        { '4', '5', '6' },
        { '1', '2', '3' }
    };

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            SDL_Rect rc = {
                g_rect_pad.x + c * (btn_w + 6),
                g_rect_pad.y + r * (btn_h + 6),
                btn_w,
                btn_h
            };
            g_vkeys.push_back({ rc, dpad_labels[r][c], dpad_keys[r][c], false, 0 });
        }
    }

    // 右側：機能キー（ESC, RUN, SPACE, ENTER, KB）
    int func_x = g_rect_pad.x + dpad_w + 10;
    int func_w = g_rect_pad.w - dpad_w - 10;
    int f_btn_h = g_rect_pad.h / 5 - 6;

    struct FuncDef {
        const char *label;
        char key;
        bool is_special;
        int special_id;
    };

    FuncDef funcs[] = {
        { "ESC",    0x1B, false, 0 },
        { "RUN(.)", '.',  false, 0 },
        { "SPACE",  ' ',  false, 0 },
        { "ENTER",  '\r', false, 0 },
        { "KEYBRD", 0,    true,  1 }
    };

    for (int i = 0; i < 5; ++i) {
        SDL_Rect rc = {
            func_x,
            g_rect_pad.y + i * (f_btn_h + 6),
            func_w,
            f_btn_h
        };
        g_vkeys.push_back({ rc, funcs[i].label, funcs[i].key, funcs[i].is_special, funcs[i].special_id });
    }
}

// メッセージログ＆メニューエリアの描画
static void draw_message_area() {
    if (!g_renderer || !g_font_msg) return;

    // エリア背景（スレートダーク）
    SDL_SetRenderDrawColor(g_renderer, 24, 28, 36, 255);
    SDL_RenderFillRect(g_renderer, &g_rect_msg);

    // 枠線（水色アクセント）
    SDL_SetRenderDrawColor(g_renderer, 70, 90, 120, 255);
    SDL_RenderDrawRect(g_renderer, &g_rect_msg);

    std::lock_guard<std::mutex> lock(g_msg_mutex);

    if (g_in_menu_mode && !g_current_menu_text.empty()) {
        // 【メニュー表示モード】
        SDL_Color col = { 255, 230, 140, 255 }; // メニューは黄色
        SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_msg, g_current_menu_text.c_str(), col);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
            if (tex) {
                SDL_Rect dst = { g_rect_msg.x + 10, g_rect_msg.y + 6, surf->w, surf->h };
                SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    } else {
        // 【通常メッセージログ表示モード（最新2行）】
        int line_y = g_rect_msg.y + 6;
        size_t count = g_message_history.size();
        size_t start = count > 2 ? count - 2 : 0;

        for (size_t i = start; i < count; ++i) {
            SDL_Color col = (i == count - 1) ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 170, 175, 185, 255 };
            SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_msg, g_message_history[i].c_str(), col);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
                if (tex) {
                    SDL_Rect dst = { g_rect_msg.x + 10, line_y, surf->w, surf->h };
                    SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
            line_y += 24;
        }
    }
}

// キーパッドの描画
static void draw_virtual_keypad() {
    if (!g_renderer || g_vkeys.empty()) return;
    TTF_Font *use_font = g_font_btn ? g_font_btn : g_font_game;

    for (const auto &btn : g_vkeys) {
        SDL_SetRenderDrawColor(g_renderer, 42, 48, 60, 255);
        SDL_RenderFillRect(g_renderer, &btn.rect);

        SDL_SetRenderDrawColor(g_renderer, 90, 110, 140, 255);
        SDL_RenderDrawRect(g_renderer, &btn.rect);

        if (use_font) {
            SDL_Color text_col = { 235, 240, 250, 255 };
            SDL_Surface *surf = TTF_RenderUTF8_Blended(use_font, btn.label, text_col);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
                if (tex) {
                    SDL_Rect dst = {
                        btn.rect.x + (btn.rect.w - surf->w) / 2,
                        btn.rect.y + (btn.rect.h - surf->h) / 2,
                        surf->w,
                        surf->h
                    };
                    SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
    }
}

static const VKey* hit_test_vkey(int x, int y) {
    for (const auto &btn : g_vkeys) {
        if (x >= btn.rect.x && x <= (btn.rect.x + btn.rect.w) &&
            y >= btn.rect.y && y <= (btn.rect.y + btn.rect.h)) {
            return &btn;
        }
    }
    return nullptr;
}

static void update_key_repeat() {
    if (g_holding_key == 0) return;

    Uint32 now = SDL_GetTicks();
    if (now - g_hold_start_time >= 250) {
        if (now - g_last_repeat_time >= 80) {
            send_key(g_holding_key);
            g_last_repeat_time = now;
        }
    }
}

static void process_sdl_events() {
    SDL_Event ev;
    int win_w = 1080, win_h = 2058;
    if (g_window) {
        SDL_GetWindowSize(g_window, &win_w, &win_h);
    }

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            quit("");
            break;

        case SDL_FINGERDOWN: {
            int px = static_cast<int>(ev.tfinger.x * win_w);
            int py = static_cast<int>(ev.tfinger.y * win_h);
            const VKey *hit = hit_test_vkey(px, py);

            if (hit) {
                if (hit->is_special) {
                    if (hit->special_id == 1) {
                        if (SDL_IsTextInputActive()) {
                            SDL_StopTextInput();
                        } else {
                            SDL_StartTextInput();
                        }
                    }
                } else {
                    send_key(hit->key);
                    g_holding_finger = ev.tfinger.fingerId;
                    g_holding_key = hit->key;
                    g_hold_start_time = SDL_GetTicks();
                    g_last_repeat_time = g_hold_start_time;
                }
            } else {
                // ゲーム画面またはメッセージエリアのタップは決定・進む
                if (py < g_rect_pad.y) {
                    send_key(' ');
                }
            }
            break;
        }

        case SDL_FINGERUP:
            if (ev.tfinger.fingerId == g_holding_finger) {
                g_holding_finger = -1;
                g_holding_key = 0;
            }
            break;

        case SDL_FINGERMOTION:
            if (ev.tfinger.fingerId == g_holding_finger) {
                int px = static_cast<int>(ev.tfinger.x * win_w);
                int py = static_cast<int>(ev.tfinger.y * win_h);
                const VKey *hit = hit_test_vkey(px, py);
                if (!hit || hit->key != g_holding_key) {
                    g_holding_finger = -1;
                    g_holding_key = 0;
                }
            }
            break;

        case SDL_TEXTINPUT:
            for (const char *p = ev.text.text; *p; ++p) {
                send_key(static_cast<unsigned char>(*p));
            }
            break;

        case SDL_KEYDOWN:
            switch (ev.key.keysym.sym) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: send_key('\r'); break;
            case SDLK_ESCAPE:
            case SDLK_AC_BACK:  send_key(0x1B); break;
            case SDLK_BACKSPACE:send_key(0x08); break;
            case SDLK_TAB:      send_key('\t'); break;
            case SDLK_SPACE:    send_key(' ');  break;
            case SDLK_UP:       send_key('8'); break;
            case SDLK_DOWN:     send_key('2'); break;
            case SDLK_LEFT:     send_key('4'); break;
            case SDLK_RIGHT:    send_key('6'); break;
            case SDLK_HOME:     send_key('7'); break;
            case SDLK_PAGEUP:   send_key('9'); break;
            case SDLK_END:      send_key('1'); break;
            case SDLK_PAGEDOWN: send_key('3'); break;
            case SDLK_KP_1:     send_key('1'); break;
            case SDLK_KP_2:     send_key('2'); break;
            case SDLK_KP_3:     send_key('3'); break;
            case SDLK_KP_4:     send_key('4'); break;
            case SDLK_KP_5:     send_key('5'); break;
            case SDLK_KP_6:     send_key('6'); break;
            case SDLK_KP_7:     send_key('7'); break;
            case SDLK_KP_8:     send_key('8'); break;
            case SDLK_KP_9:     send_key('9'); break;
            default: break;
            }
            break;

        default:
            break;
        }
    }
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
    switch (n) {
    case TERM_XTRA_CLEAR:
        if (g_renderer) {
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderClear(g_renderer);
        }
        return 0;

    case TERM_XTRA_FRESH:
        if (g_renderer) {
            draw_message_area();
            draw_virtual_keypad();
            SDL_RenderPresent(g_renderer);
        }
        return 0;

    case TERM_XTRA_EVENT:
        process_sdl_events();
        update_key_repeat();
        if (v) {
            SDL_Delay(15);
        }
        return 0;

    case TERM_XTRA_FLUSH:
    case TERM_XTRA_BORED:
        process_sdl_events();
        update_key_repeat();
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
    SDL_Rect r = {
        g_rect_game.x + x * g_cell_w,
        g_rect_game.y + y * g_cell_h,
        n * g_cell_w,
        g_cell_h
    };
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &r);
    return 0;
}

static errr Term_text_android(int x, int y, int n, byte a, concptr s) {
    (void)a;
    if (!g_renderer || !g_font_game || !s) return 0;

    std::string text(s, n);

    // 0行目（最上段）に来るメッセージを変愚蛮怒からフックして中間ログエリアに蓄積
    if (y == 0 && text.find_first_not_of(" ") != std::string::npos) {
        std::lock_guard<std::mutex> lock(g_msg_mutex);
        // 重複登録を避けて直近履歴に追加
        if (g_message_history.empty() || g_message_history.back() != text) {
            g_message_history.push_back(text);
            if (g_message_history.size() > 50) {
                g_message_history.pop_front();
            }
        }
    }

    // ゲーム画面上部の文字描画
    Term_wipe_android(x, y, n);

    SDL_Color fg = { 255, 255, 255, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_game, text.c_str(), fg);
    if (!surf) return 0;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    if (tex) {
        SDL_Rect dst = {
            g_rect_game.x + x * g_cell_w,
            g_rect_game.y + y * g_cell_h,
            surf->w,
            surf->h
        };
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

    int win_w = 1080, win_h = 2058;
    SDL_GetWindowSize(g_window, &win_w, &win_h);
    setup_layout(win_w, win_h);

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";
    
    std::vector<std::string> font_candidates = {
        files_dir + "/lib/xtra/font/NotoSansCJKjp-Regular.otf",
        files_dir + "/lib/xtra/font/NotoSansJP-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/Roboto-Regular.ttf"
    };

    // ゲーム画面用フォント（セルの高さに合わせたサイズ）
    int game_pt = static_cast<int>(g_cell_h * 0.76);
    for (const auto &path : font_candidates) {
        g_font_game = TTF_OpenFont(path.c_str(), game_pt);
        if (g_font_game) break;
    }

    // メッセージログ用フォント（少し大きめで読みやすく）
    for (const auto &path : font_candidates) {
        g_font_msg = TTF_OpenFont(path.c_str(), 18);
        if (g_font_msg) break;
    }

    // ボタン用フォント
    for (const auto &path : font_candidates) {
        g_font_btn = TTF_OpenFont(path.c_str(), 22);
        if (g_font_btn) break;
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

    LOGI("init_android succeeded! Term activated with 3-pane layout.");
    return 0;
}

extern "C" int SDL_main(int argc, char *argv[]) {
    start_logger_thread();
    LOGI("=== Hengband Mobile Starting ===");

    // 日本語環境変数の適用
    setenv("LANG", "ja_JP.UTF-8", 1);
    setenv("LC_ALL", "ja_JP.UTF-8", 1);
    setenv("LC_CTYPE", "ja_JP.UTF-8", 1);
    std::setlocale(LC_ALL, "ja_JP.UTF-8");

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

    LOGI("Calling REAL Hengband main() with: %s %s (LANG=ja_JP.UTF-8)", args[1], args[2]);

    int result = main(static_cast<int>(args.size() - 1), args.data());

    LOGI("Hengband main() finished with code: %d", result);

    for (char* arg : args) {
        if (arg) free(arg);
    }

    return result;
}
