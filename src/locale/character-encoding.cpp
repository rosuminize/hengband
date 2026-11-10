#include "locale/character-encoding.h"
#include <string>
#include <string_view>
#include <vector>

#if defined(WINDOWS)
#include <windows.h>
#elif defined(__ANDROID__) || defined(ANDROID)
#include <jni.h>
#include <SDL.h>
#else
#include <iconv.h>
#endif

#if defined(__ANDROID__) || defined(ANDROID)
// Android 標準の文字コード変換エンジン（Java Charset）を JNI 経由で呼び出す
static std::string android_convert_encoding(std::string_view src, const char *from_enc, const char *to_enc)
{
    if (src.empty()) {
        return "";
    }

    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env) {
        return std::string(src);
    }

    jbyteArray jsrc_bytes = env->NewByteArray(src.size());
    env->SetByteArrayRegion(jsrc_bytes, 0, src.size(), reinterpret_cast<const jbyte *>(src.data()));

    jclass str_class = env->FindClass("java/lang/String");
    jmethodID str_ctor = env->GetMethodID(str_class, "<init>", "([BLjava/lang/String;)V");
    jstring jfrom_enc = env->NewStringUTF(from_enc);
    jstring jstr = static_cast<jstring>(env->NewObject(str_class, str_ctor, jsrc_bytes, jfrom_enc));

    jmethodID get_bytes = env->GetMethodID(str_class, "getBytes", "(Ljava/lang/String;)[B");
    jstring jto_enc = env->NewStringUTF(to_enc);
    jbyteArray jdst_bytes = static_cast<jbyteArray>(env->CallObjectMethod(jstr, get_bytes, jto_enc));

    jsize dst_len = env->GetArrayLength(jdst_bytes);
    std::string result(dst_len, '\0');
    env->GetByteArrayRegion(jdst_bytes, 0, dst_len, reinterpret_cast<jbyte *>(result.data()));

    env->DeleteLocalRef(jsrc_bytes);
    env->DeleteLocalRef(jfrom_enc);
    env->DeleteLocalRef(jstr);
    env->DeleteLocalRef(jto_enc);
    env->DeleteLocalRef(jdst_bytes);
    env->DeleteLocalRef(str_class);

    return result;
}
#endif

/*!
 * @brief UTF-8 文字列を EUC-JP に変換する
 */
std::string to_euc_jp(std::string_view utf8_str)
{
#if defined(WINDOWS)
    if (utf8_str.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), utf8_str.size(), nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), utf8_str.size(), wstr.data(), wlen);
    int elen = WideCharToMultiByte(51932, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string euc_str(elen, 0);
    WideCharToMultiByte(51932, 0, wstr.data(), wlen, euc_str.data(), elen, nullptr, nullptr);
    return euc_str;
#elif defined(__ANDROID__) || defined(ANDROID)
    return android_convert_encoding(utf8_str, "UTF-8", "EUC-JP");
#else
    static const auto cd = iconv_open("EUC-JP", "UTF-8");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return std::string(utf8_str);
    }
    size_t in_bytes = utf8_str.size();
    size_t out_bytes = in_bytes * 2 + 1;
    std::string euc_str(out_bytes, '\0');
    char *in_buf = const_cast<char *>(utf8_str.data());
    char *out_buf = euc_str.data();
    iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes);
    euc_str.resize(out_buf - euc_str.data());
    return euc_str;
#endif
}

/*!
 * @brief EUC-JP 文字列を UTF-8 に変換する
 */
std::string to_utf8(std::string_view euc_str)
{
#if defined(WINDOWS)
    if (euc_str.empty()) return "";
    int wlen = MultiByteToWideChar(51932, 0, euc_str.data(), euc_str.size(), nullptr, 0);
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(51932, 0, euc_str.data(), euc_str.size(), wstr.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string utf8_str(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, utf8_str.data(), ulen, nullptr, nullptr);
    return utf8_str;
#elif defined(__ANDROID__) || defined(ANDROID)
    return android_convert_encoding(euc_str, "EUC-JP", "UTF-8");
#else
    static const auto cd = iconv_open("UTF-8", "EUC-JP");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return std::string(euc_str);
    }
    size_t in_bytes = euc_str.size();
    size_t out_bytes = in_bytes * 3 + 1;
    std::string utf8_str(out_bytes, '\0');
    char *in_buf = const_cast<char *>(euc_str.data());
    char *out_buf = utf8_str.data();
    iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes);
    utf8_str.resize(out_buf - utf8_str.data());
    return utf8_str;
#endif
}
