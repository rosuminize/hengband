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
        extractAssetsIfNeeded();
        try {
            // C++ 側の chdir 用にパスを環境変数に設定
            Os.setenv("FILES_DIR", getFilesDir().getAbsolutePath(), true);
            Os.setenv("ANGBAND_PATH", getFilesDir().getAbsolutePath() + "/lib/", true);
        } catch (Exception e) {
            Log.e("Hengband", "Failed to set env", e);
        }
        super.onCreate(savedInstanceState);
    }

    private void extractAssetsIfNeeded() {
        File libDir = new File(getFilesDir(), "lib");
        if (!libDir.exists()) {
            copyAssetFolder("lib", libDir);
        }
    }

    private void copyAssetFolder(String srcName, File dstDir) {
        try {
            String[] fileList = getAssets().list(srcName);
            if (fileList == null || fileList.length == 0) {
                copyAssetFile(srcName, dstDir);
            } else {
                dstDir.mkdirs();
                for (String filename : fileList) {
                    copyAssetFolder(srcName + "/" + filename, new File(dstDir, filename));
                }
            }
        } catch (IOException e) {
            Log.e("Hengband", "Asset copy error", e);
        }
    }

    private void copyAssetFile(String srcPath, File dstFile) {
        try (InputStream in = getAssets().open(srcPath);
             OutputStream out = new FileOutputStream(dstFile)) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } catch (IOException e) {
            Log.e("Hengband", "File copy error: " + srcPath, e);
        }
    }
}
