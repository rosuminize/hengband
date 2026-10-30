#include "io/uid-checker.h"

#ifdef USE_ANDROID

// Android環境ではOSのサンドボックスによりUID操作が禁止されているため、
// システムコール（setuid）を呼ばずに即時リターンする
void safe_setuid_drop()
{
}

void safe_setuid_grab()
{
}

#else

// 従来のデスクトップUnix/Windows用実装
#include "main-unix/unix-user-ids.h"
#include <unistd.h>

void safe_setuid_drop()
{
    const auto &user_ids = UnixUserIds::get_instance();
    if (setuid(user_ids.get_user_id()) != 0) {
        // エラーハンドリング
    }
}

void safe_setuid_grab()
{
    const auto &user_ids = UnixUserIds::get_instance();
    if (setuid(user_ids.get_effective_user_id()) != 0) {
        // エラーハンドリング
    }
}

#endif
