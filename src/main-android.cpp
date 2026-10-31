#include <SDL.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>

#undef main

extern int main(int argc, char *argv[]);

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

    const char *files_dir = getenv("FILES_DIR");
    if (files_dir) {
        chdir(files_dir);
        __android_log_print(ANDROID_LOG_INFO, "Hengband", "Working directory: %s", files_dir);
    }

    struct stat st;
    if (stat("lib/edit/Monster.txt", &st) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "Hengband", "Data check PASS: Monster.txt found (%lld bytes)", (long long)st.st_size);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "Hengband", "Data check FAIL: Monster.txt still not found!");
    }

    // 内部ストレージのパスを元にオプション引数を生成
    std::string base_path = files_dir ? files_dir : ".";
    std::string lib_arg = "-d" + base_path + "/lib/";
    std::string user_arg = "-du" + base_path + "/user/";

    char arg0[] = "hengband";
    char *custom_argv[] = {
        arg0,
        const_cast<char *>(lib_arg.c_str()),
        const_cast<char *>(user_arg.c_str()),
        nullptr
    };
    int custom_argc = 3;

    __android_log_print(ANDROID_LOG_INFO, "Hengband", "Args: %s %s", lib_arg.c_str(), user_arg.c_str());
    __android_log_print(ANDROID_LOG_INFO, "Hengband", "Calling Hengband main()...");

    int result = main(custom_argc, custom_argv);
    __android_log_print(ANDROID_LOG_INFO, "Hengband", "Hengband main() returned with code: %d", result);

    return result;
}
