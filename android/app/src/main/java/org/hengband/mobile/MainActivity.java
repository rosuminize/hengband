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
        extractAssetsIfNeeded();
        try {
            // 変愚蛮怒の作業ディレクトリとパスを内部ストレージに設定
            String filesPath = getFilesDir().getAbsolutePath();
            Os.chdir(filesPath);
            Os.setenv("ANGBAND_PATH", filesPath + "/lib/", true);
        } catch (Exception e) {
            Log.e("Hengband", "Failed to set environment", e);
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
