package org.hengband.mobile;

import org.libsdl.app.SDLActivity;

/**
 * SDLActivityを継承することで、SDL2が自動的に
 * 画面描画やタッチ入力イベントをC言語側へ中継してくれます。
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "hengband" // 変愚蛮怒本体のライブラリ名
        };
    }
}
