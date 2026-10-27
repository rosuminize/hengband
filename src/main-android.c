#include "z-term.h"
#include "z-virt.h"
#include <SDL2/SDL.h>

/* Pixel 8などの縦長画面を想定したレイアウト定数 */
#define VIRTUAL_SCREEN_WIDTH 1080
#define VIRTUAL_SCREEN_HEIGHT 2400
#define MAP_AREA_HEIGHT 1400  /* 上部: ターミナル描画エリア */
#define PAD_AREA_HEIGHT 1000  /* 下部: 仮想キーパッドエリア */

/* SDLのウィンドウとレンダラー */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

/* 仮想キーパッドのタッチ座標を判定してキー入力に変換する関数 */
static void handle_touch_event(int x, int y) {
    /* 画面下部（仮想キーパッドエリア）がタッチされた場合 */
    if (y > MAP_AREA_HEIGHT) {
        
        /* 画面左半分：アーケード風8方向Dパッドの判定 */
        if (x < VIRTUAL_SCREEN_WIDTH / 2) {
            int dx = x - (VIRTUAL_SCREEN_WIDTH / 4);
            int dy = y - (MAP_AREA_HEIGHT + PAD_AREA_HEIGHT / 2);
            
            /* 斜め入力の誤爆を防ぐため、中心からの角度と距離で8方向を計算 */
            if (abs(dx) > abs(dy) * 2) {
                if (dx > 0) Term_keypress('6'); else Term_keypress('4');
            } else if (abs(dy) > abs(dx) * 2) {
                if (dy > 0) Term_keypress('2'); else Term_keypress('8');
            } else {
                if (dx > 0 && dy > 0) Term_keypress('3');
                if (dx > 0 && dy < 0) Term_keypress('9');
                if (dx < 0 && dy > 0) Term_keypress('1');
                if (dx < 0 && dy < 0) Term_keypress('7');
            }
        } 
        /* 画面右半分：アクションボタンの判定 */
        else {
            /* ざっくりとした座標判定（実際はボタンの画像座標に合わせます） */
            if (y < MAP_AREA_HEIGHT + 333) {
                if (x < VIRTUAL_SCREEN_WIDTH * 3/4) Term_keypress('m'); /* 魔法 */
                else Term_keypress(27); /* ESC (キャンセル) */
            } else if (y < MAP_AREA_HEIGHT + 666) {
                if (x < VIRTUAL_SCREEN_WIDTH * 3/4) Term_keypress('i'); /* インベントリ */
                else Term_keypress('d'); /* 落とす */
            } else {
                Term_keypress(13); /* ENTER (決定) */
            }
        }
    }
}

/* SDLのイベントループを処理する関数 */
void android_event_loop(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_FINGERDOWN:
                /* タッチされた座標を仮想解像度に変換して処理 */
                handle_touch_event(
                    event.tfinger.x * VIRTUAL_SCREEN_WIDTH,
                    event.tfinger.y * VIRTUAL_SCREEN_HEIGHT
                );
                break;
            case SDL_QUIT:
                /* 終了処理 */
                break;
        }
    }
}

/* Android用の初期化処理（変愚蛮怒の本体から呼ばれる） */
errr init_android(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return -1;
    
    /* 縦画面(Portrait)固定でフルスクリーンウィンドウを作成 */
    window = SDL_CreateWindow("Hengband Mobile", 
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    
    /* SDL2の描画解像度を縦長に固定（自動スケーリング） */
    SDL_RenderSetLogicalSize(renderer, VIRTUAL_SCREEN_WIDTH, VIRTUAL_SCREEN_HEIGHT);
    
    return 0;
}
