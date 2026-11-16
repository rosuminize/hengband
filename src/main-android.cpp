#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <android/log.h>
#include <jni.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <clocale>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <vector>
#include <deque>
#include <string>
#include <thread>
#include <mutex>

// 変愚蛮怒の端末・文字コード・オプションヘッダー
#include "term/gameterm.h"
#include "term/z-term.h"
#include "term/term-color-types.h"
#include "system/angband.h"
#include "locale/character-encoding.h"
#include "game-option/map-screen-options.h"

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
static SDL_Texture *g_term_target = nullptr;

// 画面分割レイアウト座標
static SDL_Rect g_rect_game; // 上部 ゲーム画面
static SDL_Rect g_rect_msg;  // 中間 メッセージログ枠
static SDL_Rect g_rect_pad;  // 下部 操作キーパッド

// 80文字 x 24行 のセルサイズ定義
static const int V_CELL_W = 20;
static const int V_CELL_H = 40;
static const int V_TERM_W = V_CELL_W * 80; // 1600
static const int V_TERM_H = V_CELL_H * 24; // 960
static term_type g_term;

// ズーム倍率管理 (1.0 -> 1.25 -> 1.5 -> 1.75 -> 2.0 -> 1.0)
static const float ZOOM_LEVELS[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
static int g_zoom_index = 0;

// 店画面・自動キーボード状態管理
static bool g_in_store_screen = false;
static bool g_auto_keyboard_shown = false;

// メッセージログバッファ
static std::deque<std::string> g_message_history;
static std::mutex g_msg_mutex;

static inline void send_key(int ch) {
    term_key_push(ch);
}

// Android のソフトキーボードを「純粋なアルファベット専用（日本語・テンキー抑制）」に設定して開閉する
static void set_alphabet_keyboard(bool show) {
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (env) {
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (activity) {
            jclass cls = env->GetObjectClass(activity);
            jfieldID fid = env->GetStaticFieldID(cls, "mTextEdit", "Landroid/view/View;");
            if (!fid) {
                env->ExceptionClear();
            } else {
                jobject text_edit = env->GetStaticObjectField(cls, fid);
                if (text_edit) {
                    jclass tv_cls = env->FindClass("android/widget/TextView");
                    if (tv_cls) {
                        jmethodID set_input_type = env->GetMethodID(tv_cls, "setInputType", "(I)V");
                        if (set_input_type) {
                            // TYPE_CLASS_TEXT (0x01) | TYPE_TEXT_VARIATION_VISIBLE_PASSWORD (0x90) | TYPE_TEXT_FLAG_NO_SUGGESTIONS (0x80000)
                            // 日本語IMEとテンキーを封印し、QWERTY英字キーボードのみ強制表示する
                            jint flags = 0x00000001 | 0x00000090 | 0x00080000;
                            env->CallVoidMethod(text_edit, set_input_type, flags);
                        }
                        env->DeleteLocalRef(tv_cls);
                    }
                    env->DeleteLocalRef(text_edit);
                }
            }
            env->DeleteLocalRef(cls);
            env->DeleteLocalRef(activity);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    if (show) {
        if (!SDL_IsTextInputActive()) {
            SDL_StartTextInput();
        }
    } else {
        if (SDL_IsTextInputActive()) {
            SDL_StopTextInput();
        }
    }
}

// 変愚蛮怒の16色カラーパレット
static SDL_Color get_term_color(byte a) {
    switch (a & 0x0F) {
    case TERM_DARK:    return { 0,   0,   0,   255 };
    case TERM_WHITE:   return { 255, 255, 255, 255 };
    case TERM_SLATE:   return { 140, 140, 150, 255 };
    case TERM_ORANGE:  return { 255, 150, 30,  255 };
    case TERM_RED:     return { 230, 50,  50,  255 };
    case TERM_GREEN:   return { 40,  200, 70,  255 };
    case TERM_BLUE:    return { 60,  130, 255, 255 };
    case TERM_UMBER:   return { 160, 100, 50,  255 };
    case TERM_L_DARK:  return { 90,  95,  105, 255 };
    case TERM_L_WHITE: return { 210, 210, 220, 255 };
    case TERM_VIOLET:  return { 210, 80,  230, 255 };
    case TERM_YELLOW:  return { 255, 240, 60,  255 };
    case TERM_L_RED:   return { 255, 110, 110, 255 };
    case TERM_L_GREEN: return { 110, 255, 130, 255 };
    case TERM_L_BLUE:  return { 110, 210, 255, 255 };
    case TERM_L_UMBER: return { 215, 165, 110, 255 };
    default:           return { 255, 255, 255, 255 };
    }
}

struct CellChar {
    std::string utf8;
    int cell_w; // 1 (半角) or 2 (全角)
};

// start位置からlimitバイトまでの非ASCII連続領域が妥当なUTF-8列かを判定
static bool is_valid_utf8_run(const char *s, int start, int limit) {
    int k = start;
    bool parsed_complete = false;

    while (k < limit && s[k] != '\0') {
        unsigned char c1 = static_cast<unsigned char>(s[k]);
        if (c1 <= 0x7F) {
            break;
        }

        if ((c1 & 0xE0) == 0xC0 && c1 >= 0xC2) {
            if (k + 2 > limit || s[k + 1] == '\0') {
                return parsed_complete;
            }
            unsigned char c2 = static_cast<unsigned char>(s[k + 1]);
            if ((c2 & 0xC0) != 0x80) return false;
            k += 2;
            parsed_complete = true;
        } else if ((c1 & 0xF0) == 0xE0) {
            if (k + 3 > limit || s[k + 1] == '\0' || s[k + 2] == '\0') {
                return parsed_complete;
            }
            unsigned char c2 = static_cast<unsigned char>(s[k + 1]);
            unsigned char c3 = static_cast<unsigned char>(s[k + 2]);
            if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            if (c1 == 0xE0 && c2 < 0xA0) return false;
            if (c1 == 0xED && c2 > 0x9F) return false;
            k += 3;
            parsed_complete = true;
        } else if ((c1 & 0xF8) == 0xF0 && c1 <= 0xF4) {
            if (k + 4 > limit || s[k + 1] == '\0' || s[k + 2] == '\0' || s[k + 3] == '\0') {
                return parsed_complete;
            }
            unsigned char c2 = static_cast<unsigned char>(s[k + 1]);
            unsigned char c3 = static_cast<unsigned char>(s[k + 2]);
            unsigned char c4 = static_cast<unsigned char>(s[k + 3]);
            if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) return false;
            if (c1 == 0xF0 && c2 < 0x90) return false;
            if (c1 == 0xF4 && c2 > 0x8F) return false;
            k += 4;
            parsed_complete = true;
        } else {
            return false;
        }
    }

    return parsed_complete;
}

// バイト列 (s, n_bytes) から UTF-8 と EUC-JP を自動判別して1文字ずつ分解する
static std::vector<CellChar> parse_mixed_bytes(const char *s, int n_bytes, std::string &out_full_utf8) {
    std::vector<CellChar> result;
    out_full_utf8.clear();
    if (!s || n_bytes <= 0) return result;

    int i = 0;
    while (i < n_bytes && s[i] != '\0') {
        unsigned char c1 = static_cast<unsigned char>(s[i]);

        if (c1 <= 0x7F) {
            std::string ch(1, static_cast<char>(c1));
            out_full_utf8 += ch;
            result.push_back({ std::move(ch), 1 });
            i += 1;
            continue;
        }

        if (is_valid_utf8_run(s, i, n_bytes)) {
            while (i < n_bytes && s[i] != '\0' && static_cast<unsigned char>(s[i]) > 0x7F) {
                unsigned char uc = static_cast<unsigned char>(s[i]);
                int u_len = 1;
                int cw = 2;
                if ((uc & 0xE0) == 0xC0) { u_len = 2; cw = 1; }
                else if ((uc & 0xF0) == 0xE0) { u_len = 3; cw = 2; }
                else if ((uc & 0xF8) == 0xF0) { u_len = 4; cw = 2; }

                if (i + u_len > n_bytes) {
                    i = n_bytes;
                    break;
                }
                bool has_null = false;
                for (int b = 0; b < u_len; ++b) {
                    if (s[i + b] == '\0') { has_null = true; break; }
                }
                if (has_null) {
                    i = n_bytes;
                    break;
                }

                std::string ch(s + i, u_len);
                out_full_utf8 += ch;
                result.push_back({ std::move(ch), cw });
                i += u_len;
            }
            continue;
        }

        int euc_start = i;
        struct EucSpan {
            int cell_w;
        };
        std::vector<EucSpan> spans;

        while (i < n_bytes && s[i] != '\0' && static_cast<unsigned char>(s[i]) > 0x7F) {
            if (!spans.empty() && is_valid_utf8_run(s, i, n_bytes)) {
                break;
            }

            unsigned char ec = static_cast<unsigned char>(s[i]);
            if (ec == 0x8E) {
                if (i + 2 <= n_bytes && s[i + 1] != '\0') {
                    spans.push_back({ 1 });
                    i += 2;
                } else {
                    i = n_bytes;
                    break;
                }
            } else if (ec == 0x8F) {
                if (i + 3 <= n_bytes && s[i + 1] != '\0' && s[i + 2] != '\0') {
                    spans.push_back({ 2 });
                    i += 3;
                } else {
                    i = n_bytes;
                    break;
                }
            } else if (ec >= 0xA1 && ec <= 0xFE) {
                if (i + 2 <= n_bytes && s[i + 1] != '\0') {
                    spans.push_back({ 2 });
                    i += 2;
                } else {
                    i = n_bytes;
                    break;
                }
            } else {
                spans.push_back({ 1 });
                i += 1;
            }
        }

        int euc_bytes = i - euc_start;
        if (euc_bytes > 0 && !spans.empty()) {
            auto converted = sys_to_utf8(std::string_view(s + euc_start, euc_bytes));
            if (converted && !converted->empty()) {
                out_full_utf8 += *converted;
                size_t u_pos = 0;
                for (const auto &sp : spans) {
                    if (u_pos >= converted->size()) break;
                    unsigned char uc = static_cast<unsigned char>((*converted)[u_pos]);
                    size_t u_len = 1;
                    if ((uc & 0xE0) == 0xC0) u_len = 2;
                    else if ((uc & 0xF0) == 0xE0) u_len = 3;
                    else if ((uc & 0xF8) == 0xF0) u_len = 4;

                    if (u_pos + u_len > converted->size()) break;
                    result.push_back({ converted->substr(u_pos, u_len), sp.cell_w });
                    u_pos += u_len;
                }
            } else {
                for (const auto &sp : spans) {
                    std::string sp_str(sp.cell_w, ' ');
                    out_full_utf8 += sp_str;
                    result.push_back({ std::move(sp_str), sp.cell_w });
                }
            }
        }
    }

    return result;
}

// z-term の行バッファを参照し、UTF-8(3バイト=2セル)によるX座標のズレを補正した真のセルX座標を計算する
static int get_visual_cell_x(int y, int raw_x) {
    if (!g_term.scr || !g_term.scr->c || y < 0 || y >= g_term.hgt || raw_x <= 0) {
        return raw_x;
    }
    const char *row = g_term.scr->c[y];
    if (!row) return raw_x;

    int limit = (raw_x < g_term.wid) ? raw_x : g_term.wid;
    std::string dummy;
    auto cells = parse_mixed_bytes(row, limit, dummy);
    int visual_x = 0;
    for (const auto &c : cells) {
        visual_x += c.cell_w;
    }
    return visual_x;
}

// --- バーチャルキーパッド定義 ---
struct VKey {
    SDL_Rect rect;
    const char *label;
    char key;
    bool is_special;
    int special_id; // 1: KEYBRD, 2: ZOOM
};

static std::vector<VKey> g_vkeys;
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

static void setup_layout(int win_w, int win_h) {
    // 1. 上部：80文字完全収容（1080 x 648）
    g_rect_game.x = 0;
    g_rect_game.y = 4;
    g_rect_game.w = win_w;
    g_rect_game.h = static_cast<int>(win_w * 960.0f / 1600.0f); // 648px

    // 2. 中間：メッセージログ枠（260px）
    g_rect_msg.x = 8;
    g_rect_msg.y = g_rect_game.y + g_rect_game.h + 8;
    g_rect_msg.w = win_w - 16;
    g_rect_msg.h = 260;

    // 3. 下部：バーチャルキーパッド
    g_rect_pad.x = 8;
    g_rect_pad.y = g_rect_msg.y + g_rect_msg.h + 8;
    g_rect_pad.w = win_w - 16;
    g_rect_pad.h = win_h - g_rect_pad.y - 12;

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

    int func_x = g_rect_pad.x + dpad_w + 8;
    int func_w = g_rect_pad.w - dpad_w - 8;
    int f_btn_h = g_rect_pad.h / 5 - 6;

    struct FuncDef {
        const char *label;
        char key;
        bool is_special;
        int special_id;
    };

    // 右側ボタン並び順: ESC, SPACE, ENTER, KEYBRD, ZOOM
    FuncDef funcs[] = {
        { "ESC",    0x1B, false, 0 },
        { "SPACE",  ' ',  false, 0 },
        { "ENTER",  '\r', false, 0 },
        { "KEYBRD", 0,    true,  1 },
        { "ZOOM",   0,    true,  2 }
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

static void draw_middle_area() {
    if (!g_renderer || !g_font_msg) return;

    SDL_SetRenderDrawColor(g_renderer, 18, 22, 30, 255);
    SDL_RenderFillRect(g_renderer, &g_rect_msg);

    SDL_SetRenderDrawColor(g_renderer, 65, 95, 140, 255);
    SDL_RenderDrawRect(g_renderer, &g_rect_msg);

    std::lock_guard<std::mutex> lock(g_msg_mutex);
    int line_y = g_rect_msg.y + 8;
    int max_draw_w = g_rect_msg.w - 20;

    std::vector<std::string> wrapped_lines;
    for (const auto &raw : g_message_history) {
        std::string cur = "";
        for (size_t i = 0; i < raw.size(); ) {
            unsigned char c = static_cast<unsigned char>(raw[i]);
            size_t c_len = 1;
            if ((c & 0xE0) == 0xC0) c_len = 2;
            else if ((c & 0xF0) == 0xE0) c_len = 3;
            else if ((c & 0xF8) == 0xF0) c_len = 4;

            if (i + c_len > raw.size()) break;
            std::string ch = raw.substr(i, c_len);

            int text_w = 0, text_h = 0;
            TTF_SizeUTF8(g_font_msg, (cur + ch).c_str(), &text_w, &text_h);
            if (text_w > max_draw_w) {
                wrapped_lines.push_back(cur);
                cur = ch;
            } else {
                cur += ch;
            }
            i += c_len;
        }
        if (!cur.empty()) wrapped_lines.push_back(cur);
    }

    size_t count = wrapped_lines.size();
    size_t max_lines = 7;
    size_t start = count > max_lines ? count - max_lines : 0;

    for (size_t i = start; i < count; ++i) {
        SDL_Color col = (i == count - 1) ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 150, 160, 175, 255 };
        SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_msg, wrapped_lines[i].c_str(), col);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
            if (tex) {
                SDL_Rect dst = { g_rect_msg.x + 10, line_y, surf->w, surf->h };
                SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        line_y += 32;
        if (line_y + 30 > g_rect_msg.y + g_rect_msg.h) break;
    }
}

static void draw_virtual_keypad() {
    if (!g_renderer || !g_font_btn || g_vkeys.empty()) return;

    char zoom_label_buf[32];
    std::snprintf(zoom_label_buf, sizeof(zoom_label_buf), "ZOOM %.2gx", ZOOM_LEVELS[g_zoom_index]);

    for (const auto &btn : g_vkeys) {
        SDL_SetRenderDrawColor(g_renderer, 38, 44, 56, 255);
        SDL_RenderFillRect(g_renderer, &btn.rect);

        SDL_SetRenderDrawColor(g_renderer, 85, 105, 135, 255);
        SDL_RenderDrawRect(g_renderer, &btn.rect);

        const char *disp_label = (btn.is_special && btn.special_id == 2) ? zoom_label_buf : btn.label;
        SDL_Color text_col = { 240, 245, 255, 255 };
        SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_btn, disp_label, text_col);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
            if (tex) {
                int draw_w = surf->w > (btn.rect.w - 8) ? (btn.rect.w - 8) : surf->w;
                SDL_Rect dst = {
                    btn.rect.x + (btn.rect.w - draw_w) / 2,
                    btn.rect.y + (btn.rect.h - surf->h) / 2,
                    draw_w,
                    surf->h
                };
                SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
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
                        // KEYBRD: アルファベット専用キーボードを手動トグル
                        bool next_state = !SDL_IsTextInputActive();
                        set_alphabet_keyboard(next_state);
                        if (!next_state) {
                            g_auto_keyboard_shown = false;
                        }
                    } else if (hit->special_id == 2) {
                        // ZOOM: 1.0 -> 1.25 -> 1.5 -> 1.75 -> 2.0 -> 1.0
                        g_zoom_index = (g_zoom_index + 1) % 5;
                    }
                } else {
                    send_key(hit->key);
                    g_holding_finger = ev.tfinger.fingerId;
                    g_holding_key = hit->key;
                    g_hold_start_time = SDL_GetTicks();
                    g_last_repeat_time = g_hold_start_time;
                }
            } else {
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
        if (g_renderer && g_term_target) {
            SDL_SetRenderTarget(g_renderer, g_term_target);
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderClear(g_renderer);
            SDL_SetRenderTarget(g_renderer, nullptr);
        }
        // 画面全消去＝店画面の終了・切り替えとみなして自動キーボードを閉じる
        if (g_in_store_screen) {
            g_in_store_screen = false;
            if (g_auto_keyboard_shown) {
                set_alphabet_keyboard(false);
                g_auto_keyboard_shown = false;
            }
        }
        return 0;

    case TERM_XTRA_FRESH:
        if (g_renderer && g_term_target) {
            SDL_SetRenderTarget(g_renderer, nullptr);
            SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(g_renderer, 8, 10, 14, 255);
            SDL_RenderClear(g_renderer);

            // ゲーム画面領域でクリッピングし、画面中心を基準に現在のズーム倍率で拡大描画（見切れ補正なし）
            SDL_RenderSetClipRect(g_renderer, &g_rect_game);
            float zoom = ZOOM_LEVELS[g_zoom_index];
            int dst_w = static_cast<int>(g_rect_game.w * zoom);
            int dst_h = static_cast<int>(g_rect_game.h * zoom);
            int dst_x = g_rect_game.x + (g_rect_game.w - dst_w) / 2;
            int dst_y = g_rect_game.y + (g_rect_game.h - dst_h) / 2;
            SDL_Rect zoom_dst = { dst_x, dst_y, dst_w, dst_h };
            SDL_RenderCopy(g_renderer, g_term_target, nullptr, &zoom_dst);
            SDL_RenderSetClipRect(g_renderer, nullptr);

            draw_middle_area();
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

// セル単位の完全クリア（足跡・ゴミの消去）
static errr Term_wipe_android(int x, int y, int n) {
    if (!g_renderer || !g_term_target || n <= 0) return 0;
    int vx = get_visual_cell_x(y, x);
    if (vx >= 80) return 0;
    int vw = (vx + n > 80) ? (80 - vx) : n;

    SDL_SetRenderTarget(g_renderer, g_term_target);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_Rect r = { vx * V_CELL_W, y * V_CELL_H, vw * V_CELL_W, V_CELL_H };
    SDL_RenderFillRect(g_renderer, &r);
    SDL_SetRenderTarget(g_renderer, nullptr);
    return 0;
}

// 座標補正付き UTF-8 / EUC-JP 自動判別・1文字ずつ厳密セル配置レンダリング
static errr Term_text_android(int x, int y, int n, byte a, concptr s) {
    if (!g_renderer || !g_font_game || !g_term_target || !s || n <= 0) return 0;

    // 1. バイト長 n の範囲内で UTF-8 と EUC-JP を自動判別して分解
    std::string full_utf8;
    std::vector<CellChar> cells = parse_mixed_bytes(s, n, full_utf8);

    // 2. 店の入退店を検知してアルファベットキーボードを自動切り替え
    if (y >= 20 && y <= 23) {
        if (full_utf8.find("購入") != std::string::npos ||
            full_utf8.find("売却") != std::string::npos ||
            full_utf8.find("店から出る") != std::string::npos ||
            full_utf8.find("置く") != std::string::npos ||
            full_utf8.find("取る") != std::string::npos) {
            if (!g_in_store_screen) {
                g_in_store_screen = true;
                if (!SDL_IsTextInputActive()) {
                    set_alphabet_keyboard(true);
                    g_auto_keyboard_shown = true;
                }
            }
        }
    } else if (y >= 1 && y <= 5 && full_utf8.find("HP") != std::string::npos) {
        // 通常マップのステータス欄が再描画されたら店から出たと判定して操作パネルに戻す
        if (g_in_store_screen) {
            g_in_store_screen = false;
            if (g_auto_keyboard_shown) {
                set_alphabet_keyboard(false);
                g_auto_keyboard_shown = false;
            }
        }
    }

    // 3. ゲームメッセージ（y == 0）のみをトリムしてクリーンに蓄積
    if (y == 0) {
        size_t last_non_space = full_utf8.find_last_not_of(' ');
        if (last_non_space != std::string::npos) {
            std::string trimmed = full_utf8.substr(0, last_non_space + 1);
            std::lock_guard<std::mutex> lock(g_msg_mutex);
            if (g_message_history.empty() || g_message_history.back() != trimmed) {
                g_message_history.push_back(std::move(trimmed));
                if (g_message_history.size() > 50) {
                    g_message_history.pop_front();
                }
            }
        }
    }

    // 4. UTF-8 バイト数と表示セル幅の差によるX座標ズレを補正した真の開始位置を算出
    int vx = get_visual_cell_x(y, x);
    int total_cells = 0;
    for (const auto &c : cells) {
        total_cells += c.cell_w;
    }
    int wipe_cells = (total_cells > 0) ? total_cells : n;

    if (vx < 80) {
        int vw = (vx + wipe_cells > 80) ? (80 - vx) : wipe_cells;
        SDL_SetRenderTarget(g_renderer, g_term_target);
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_Rect r = { vx * V_CELL_W, y * V_CELL_H, vw * V_CELL_W, V_CELL_H };
        SDL_RenderFillRect(g_renderer, &r);
    }

    // 5. 1文字ずつ本来のセル枠（半角=20px, 全角=40px）内に厳密配置して描画
    SDL_SetRenderTarget(g_renderer, g_term_target);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_Color fg = get_term_color(a);

    int cur_x = vx;
    for (const auto &cell : cells) {
        if (cur_x >= 80) break;
        if (cell.utf8 != " " && cell.utf8 != " ") {
            SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font_game, cell.utf8.c_str(), fg);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(g_renderer, surf);
                if (tex) {
                    int box_w = cell.cell_w * V_CELL_W;
                    int draw_w = surf->w > box_w ? box_w : surf->w;
                    int draw_h = surf->h > V_CELL_H ? V_CELL_H : surf->h;
                    SDL_Rect dst = {
                        cur_x * V_CELL_W + (box_w - draw_w) / 2,
                        y * V_CELL_H + (V_CELL_H - draw_h) / 2,
                        draw_w,
                        draw_h
                    };
                    SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
        }
        cur_x += cell.cell_w;
    }

    SDL_SetRenderTarget(g_renderer, nullptr);
    return 0;
}

// --- Android ディスプレイモジュールの初期化 ---

extern "C" errr init_android(int argc, char **argv) {
    (void)argc; (void)argv;
    LOGI("=== init_android() called ===");

    // IME の余計な変換候補・日本語予測を抑制
    SDL_SetHint(SDL_HINT_IME_INTERNAL_EDITING, "0");

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

    g_term_target = SDL_CreateTexture(g_renderer,
                                      SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET,
                                      V_TERM_W, V_TERM_H);

    int win_w = 1080, win_h = 2058;
    SDL_GetWindowSize(g_window, &win_w, &win_h);
    setup_layout(win_w, win_h);

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";

    std::vector<std::string> font_candidates = {
        files_dir + "/lib/xtra/font/NotoSansCJKjp-Regular.otf",
        files_dir + "/lib/xtra/font/NotoSansJP-Regular.ttf",
        "/system/fonts/NotoSansMonoCJKjp-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/Roboto-Regular.ttf"
    };

    for (const auto &path : font_candidates) {
        g_font_game = TTF_OpenFont(path.c_str(), 28);
        if (g_font_game) break;
    }

    for (const auto &path : font_candidates) {
        g_font_msg = TTF_OpenFont(path.c_str(), 20);
        if (g_font_msg) break;
    }

    for (const auto &path : font_candidates) {
        g_font_btn = TTF_OpenFont(path.c_str(), 32);
        if (g_font_btn) break;
    }

    // 常にプレイヤーを中心に画面描画するオプションをデフォルト有効化
    center_player = true;

    // 内部バッファ幅はUTF-8の3バイト文字が収まるよう余裕を持たせて初期化（表示は80列）
    term_type *t = &g_term;
    term_init(t, 160, 24, 256);

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

    LOGI("init_android succeeded! Zoom, center_player, and store auto-alphabet-keyboard active.");
    return 0;
}

extern "C" int SDL_main(int argc, char *argv[]) {
    start_logger_thread();
    LOGI("=== Hengband Mobile Starting ===");

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
