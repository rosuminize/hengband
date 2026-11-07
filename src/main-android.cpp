#include <SDL.h>
#include <SDL_main.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

// ★超重要: SDLが仕掛けてくる「#define main SDL_main」を解除し、
// 変愚蛮怒本体の main() と名前が衝突して無限ループするのを防ぐ
#ifdef main
#undef main
#endif

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 変愚蛮怒本体のエントリポイント（本物の main を呼ぶ）
extern int main(int argc, char *argv[]);

extern "C" int SDL_main(int argc, char *argv[]) {
    LOGI("=== Hengband Mobile Starting ===");

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";

    if (chdir(files_dir.c_str()) != 0) {
        LOGE("Failed to chdir to: %s", files_dir.c_str());
    }
    LOGI("Working directory: %s", files_dir.c_str());

    std::string lib_dir = files_dir + "/lib/";
    std::string user_dir = files_dir + "/user/";

    // 環境変数による基底パス指定
    setenv("HOME", files_dir.c_str(), 1);
    setenv("ANGBAND_PATH", lib_dir.c_str(), 1);

    // データ整合性チェック
    std::string test_path = lib_dir + "edit/Monster.txt";
    std::ifstream test_file(test_path);
    if (test_file.is_open()) {
        LOGI("Data check PASS: Monster.txt found (%s)", test_path.c_str());
        test_file.close();
    } else {
        LOGE("Data check FAIL: Monster.txt still not found!");
    }

    // 引数の構築（-duser のみ渡す）
    std::string arg_user = "-duser=" + user_dir;

    std::vector<char*> args;
    args.push_back(strdup("hengband"));
    args.push_back(strdup(arg_user.c_str()));
    args.push_back(nullptr);

    LOGI("Calling REAL Hengband main() with: %s", args[1]);

    // 変愚蛮怒本体のメイン関数を実行
    int result = main(static_cast<int>(args.size() - 1), args.data());

    LOGI("Hengband main() finished with code: %d", result);

    for (char* arg : args) {
        if (arg) free(arg);
    }

    return result;
}
