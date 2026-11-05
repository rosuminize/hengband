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

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 変愚蛮怒本体のエントリポイント
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

    // 環境変数による基底パス指定（変愚蛮怒標準）
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

    // 引数の構築：-dlib は使わず、-duser のみ渡す
    std::string arg_user = "-duser=" + user_dir;

    std::vector<char*> args;
    args.push_back(strdup("hengband"));
    args.push_back(strdup(arg_user.c_str()));
    args.push_back(nullptr);

    LOGI("Calling main with: %s", args[1]);

    // 変愚蛮怒本体のメインループを開始
    int result = main(args.size() - 1, args.data());

    for (char* arg : args) {
        if (arg) free(arg);
    }

    return result;
}
