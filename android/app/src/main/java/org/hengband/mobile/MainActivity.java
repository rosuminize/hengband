package org.hengband.mobile;

import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import org.libsdl.app.SDLActivity;
import java.io.*;

public class MainActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "hengband"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String filesPath = getFilesDir().getAbsolutePath();

        // 1. assets/lib を内部ストレージ files/lib へ展開
        extractAssetsRecursive("lib", new File(getFilesDir(), "lib"));

        // 2. 変愚蛮怒が探索する ~/.angband/Hengband フォルダを事前作成
        File angbandDir = new File(getFilesDir(), ".angband/Hengband");
        if (!angbandDir.exists()) {
            angbandDir.mkdirs();
        }

        // 3. 環境変数の設定 (HOME を内部ストレージにして ~ 解決を可能にする)
        try {
            Os.setenv("HOME", filesPath, true);
            Os.setenv("FILES_DIR", filesPath, true);
            Os.setenv("ANGBAND_PATH", filesPath + "/lib/", true);
        } catch (Exception e) {
            Log.e("Hengband", "Failed to set env", e);
        }

        super.onCreate(savedInstanceState);
    }

    private void extractAssetsRecursive(String path, File target) {
        try {
            String[] list = getAssets().list(path);
            if (list == null || list.length == 0) {
                copyAssetFile(path, target);
            } else {
                if (!target.exists()) {
                    target.mkdirs();
                }
                for (String child : list) {
                    extractAssetsRecursive(path + "/" + child, new File(target, child));
                }
            }
        } catch (IOException e) {
            Log.e("Hengband", "Extraction error at: " + path, e);
        }
    }

    private void copyAssetFile(String srcPath, File dstFile) {
        if (dstFile.exists() && dstFile.length() > 0) {
            return;
        }
        File parent = dstFile.getParentFile();
        if (parent != null && !parent.exists()) {
            parent.mkdirs();
        }
        try (InputStream in = getAssets().open(srcPath);
             OutputStream out = new FileOutputStream(dstFile)) {
            byte[] buf = new byte[8192];
            int len;
            while ((len = in.read(buf)) != -1) {
                out.write(buf, 0, len);
            }
        } catch (FileNotFoundException ignored) {
        } catch (IOException e) {
            Log.e("Hengband", "Failed copy: " + srcPath, e);
        }
    }
}
