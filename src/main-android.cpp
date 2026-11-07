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
#include <thread>

// SDL2のマクロによる無限再帰トラップを解除
#ifdef main
#undef main
#endif

#define LOG_TAG "Hengband"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int main(int argc, char *argv[]);

// 変愚蛮怒本体の標準出力・標準エラー出力をlogcatにリアルタイム転送
static void start_logger_thread() {
    int pfd[2];
    if (pipe(pfd) == -1) return;

    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    close(pfd[1]);

    std::thread([read_fd = pfd[0]]() {
        char buf[1024];
        std::string line;
        ssize_t r;
        while ((r = read(read_fd, buf, sizeof(buf) - 1)) > 0) {
            buf[r] = '\0';
            line += buf;
            size_t pos;
            while ((pos = line.find('\n')) != std::string::npos) {
                std::string msg = line.substr(0, pos);
                LOGI("[Core] %s", msg.c_str());
                line.erase(0, pos + 1);
            }
        }
    }).detach();
}

extern "C" int SDL_main(int argc, char *argv[]) {
    start_logger_thread();
    LOGI("=== Hengband Mobile Starting ===");

    const char *files_dir_env = getenv("FILES_DIR");
    std::string files_dir = files_dir_env ? files_dir_env : "/data/user/0/org.hengband.mobile/files";

    if (chdir(files_dir.c_str()) != 0) {
        LOGE("Failed to chdir to: %s", files_dir.c_str());
    }
    LOGI("Working directory: %s", files_dir.c_str());

    std::string lib_dir = files_dir + "/lib/";
    std::string user_dir = files_dir + "/user/";

    setenv("HOME", files_dir.c_str(), 1);
    setenv("ANGBAND_PATH", lib_dir.c_str(), 1);

    // v3系モンスター定義ファイル（MonraceDefinitions.jsonc）の存在確認
    std::string test_path = lib_dir + "edit/MonraceDefinitions.jsonc";
    std::ifstream test_file(test_path);
    if (test_file.is_open()) {
        LOGI("Data check PASS: MonraceDefinitions.jsonc found (%s)", test_path.c_str());
        test_file.close();
    } else {
        LOGE("Data check FAIL: MonraceDefinitions.jsonc not found!");
    }

    // 引数に -msdl2 を渡してSDL2グラフィックモジュールを初期化
    std::string arg_mod = "-msdl2";
    std::string arg_user = "-duser=" + user_dir;

    std::vector<char*> args;
    args.push_back(strdup("hengband"));
    args.push_back(strdup(arg_mod.c_str()));
    args.push_back(strdup(arg_user.c_str()));
    args.push_back(nullptr);

    LOGI("Calling REAL Hengband main() with: %s %s", args[1], args[2]);

    int result = main(static_cast<int>(args.size() - 1), args.data());

    LOGI("Hengband main() finished with code: %d", result);

    for (char* arg : args) {
        if (arg) free(arg);
    }

    return result;
}
