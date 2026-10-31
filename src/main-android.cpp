#include <SDL.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#undef main

extern int main(int argc, char *argv[]);

// stdout と stderr を Android の logcat にリアルタイム転送するスレッド
static int pfd[2];
static pthread_t logging_thread;

static void *logger_thread_func(void *) {
    ssize_t rdsz;
    char buf[2048];
    while ((rdsz = read(pfd[0], buf, sizeof(buf) - 1)) > 0) {
        if (buf[rdsz - 1] == '\n') rdsz--;
        buf[rdsz] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "Hengband", buf);
    }
    return nullptr;
}

static void start_stdio_redirection() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    pipe(pfd);
    dup2(pfd[1], STDOUT_FILENO);
    dup2(pfd[1], STDERR_FILENO);
    pthread_create(&logging_thread, nullptr, logger_thread_func, nullptr);
    pthread_detach(logging_thread);
}

extern "C" int SDL_main(int argc, char *argv[]) {
    start_stdio_redirection();

    __android_log_print(ANDROID_LOG_INFO, "Hengband", "=== Hengband Mobile Starting ===");

    // アプリ内部ストレージへの移動
    const char *files_dir = getenv("FILES_DIR");
    if (files_dir) {
        chdir(files_dir);
        __android_log_print(ANDROID_LOG_INFO, "Hengband", "Current directory changed to: %s", files_dir);
    }

    // データファイルの存在確認テスト
    struct stat st;
    if (stat("lib/edit/Monster.txt", &st) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "Hengband", "Data check PASS: Monster.txt exists (%lld bytes)", (long long)st.st_size);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "Hengband", "Data check FAIL: Cannot find lib/edit/Monster.txt!");
    }

    // 不正な引数で落ちないよう、引数なし（デフォルト起動）で main を呼ぶ
    char arg0[] = "hengband";
    char *custom_argv[] = { arg0, nullptr };
    int custom_argc = 1;

    __android_log_print(ANDROID_LOG_INFO, "Hengband", "Calling Hengband main()...");
    int result = main(custom_argc, custom_argv);
    __android_log_print(ANDROID_LOG_INFO, "Hengband", "Hengband main() returned with code: %d", result);

    return result;
}
