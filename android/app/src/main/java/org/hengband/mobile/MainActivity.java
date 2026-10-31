package org.hengband.mobile;

import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

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
        extractAssetsRecursive("lib", new File(getFilesDir(), "lib"));

        // ユーザーディレクトリ用フォルダ（セーブデータ・設定等）を事前に作成
        File userDir = new File(getFilesDir(), "user");
        if (!userDir.exists()) {
            userDir.mkdirs();
        }

        try {
            Os.setenv("FILES_DIR", getFilesDir().getAbsolutePath(), true);
            Os.setenv("ANGBAND_PATH", getFilesDir().getAbsolutePath() + "/lib/", true);
        } catch (Exception e) {
            Log.e("Hengband", "Failed to set env", e);
        }
        super.onCreate(savedInstanceState);
    }

    private void extractAssetsRecursive(String assetPath, File targetDir) {
        try {
            String[] list = getAssets().list(assetPath);
            if (list == null || list.length == 0) {
                // ファイルの場合はコピー
                copyAssetFile(assetPath, targetDir);
            } else {
                // ディレクトリの場合は作成して再帰
                if (!targetDir.exists()) {
                    targetDir.mkdirs();
                }
                for (String file : list) {
                    extractAssetsRecursive(assetPath + "/" + file, new File(targetDir, file));
                }
            }
        } catch (IOException e) {
            Log.e("Hengband", "Asset extraction error at: " + assetPath, e);
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
        } catch (IOException e) {
            Log.e("Hengband", "Failed copying file: " + srcPath, e);
        }
    }
}
