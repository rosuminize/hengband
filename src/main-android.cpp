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
#include <pthread.h>

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 変愚蛮怒本体のエントリポイント
extern int main(int argc, char *argv[]);

// スレッドに渡す引数構造体
struct HengbandThreadArgs {
    int argc;
    std::vector<char*> args;
    int result;
};

// 8MBスタック上で実行されるメイン関数
void* hengband_main_thread(void* param) {
    auto* thread_args = static_cast<HengbandThreadArgs*>(param);
    LOGI("Hengband thread started with 8MB stack. Entering main()...");
    thread_args->result = main(thread_args->argc, thread_args->args.data());
    LOGI("Hengband main() returned with code: %d", thread_args->result);
    return nullptr;
}

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

    // 引数の構築
    std::string arg_user = "-duser=" + user_dir;

    HengbandThreadArgs targs;
    targs.args.push_back(strdup("hengband"));
    targs.args.push_back(strdup(arg_user.c_str()));
    targs.args.push_back(nullptr);
    targs.argc = static_cast<int>(targs.args.size() - 1);
    targs.result = 0;

    LOGI("Calling main via 8MB stack thread with: %s", targs.args[1]);

    // スタックサイズを 8MB (8 * 1024 * 1024 bytes) に拡張したスレッド属性を作成
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);

    pthread_t thread;
    int rc = pthread_create(&thread, &attr, hengband_main_thread, &targs);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        LOGE("Failed to create 8MB stack thread: %d. Falling back to direct call.", rc);
        targs.result = main(targs.argc, targs.args.data());
    } else {
        // メインスレッドは変愚蛮怒スレッドの終了を待機
        pthread_join(thread, nullptr);
    }

    for (char* arg : targs.args) {
        if (arg) free(arg);
    }

    return targs.result;
}
