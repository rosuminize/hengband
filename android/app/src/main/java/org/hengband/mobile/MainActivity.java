package org.hengband.mobile;

import android.content.Context;
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

        File libTargetDir = new File(getFilesDir(), "lib");
        if (!libTargetDir.exists()) {
            libTargetDir.mkdirs();
        }

        // assets/lib がある場合はそれを、なければ直下の edit 等を files/lib に展開
        try {
            String[] rootAssets = getAssets().list("");
            boolean hasLibFolder = false;
            if (rootAssets != null) {
                for (String name : rootAssets) {
                    if ("lib".equals(name)) {
                        hasLibFolder = true;
                        break;
                    }
                }
            }

            if (hasLibFolder) {
                extractAssetsRecursive("lib", libTargetDir);
            } else {
                String[] angbandDirs = {"apex", "bone", "data", "edit", "file", "fonts", "help", "info", "pref", "script", "user", "xtra"};
                for (String dir : angbandDirs) {
                    extractAssetsRecursive(dir, new File(libTargetDir, dir));
                }
            }
        } catch (IOException e) {
            Log.e("Hengband", "Asset check failed", e);
        }

        // ユーザーディレクトリの事前作成
        File userDir = new File(getFilesDir(), "user");
        if (!userDir.exists()) {
            userDir.mkdirs();
        }

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
