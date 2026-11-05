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

        File userDir = new File(getFilesDir(), "user");
        if (!userDir.exists()) {
            userDir.mkdirs();
        }

        // assets/lib を files/lib に展開
        extractGameAssets(libTargetDir);

        try {
            Os.setenv("HOME", filesPath, true);
            Os.setenv("FILES_DIR", filesPath, true);
            Os.setenv("ANGBAND_PATH", filesPath + "/lib/", true);
        } catch (Exception e) {
            Log.e("Hengband", "Failed to set env", e);
        }

        super.onCreate(savedInstanceState);
    }

    private void extractGameAssets(File libTargetDir) {
        try {
            // list("") を介さず、直に "lib" 内を走査
            String[] libFiles = getAssets().list("lib");
            if (libFiles != null && libFiles.length > 0) {
                Log.i("Hengband", "Found assets/lib (" + libFiles.length + " entries). Extracting...");
                extractAssetsRecursive("lib", libTargetDir);
            } else {
                Log.i("Hengband", "assets/lib empty or flat. Checking standard subdirectories...");
                String[] angbandDirs = {"apex", "bone", "data", "edit", "file", "fonts", "help", "info", "pref", "script", "user", "xtra"};
                for (String dir : angbandDirs) {
                    extractAssetsRecursive(dir, new File(libTargetDir, dir));
                }
            }
            Log.i("Hengband", "Asset extraction finished.");
        } catch (IOException e) {
            Log.e("Hengband", "Asset extraction failed", e);
        }
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
