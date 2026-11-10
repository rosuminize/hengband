/*!
 *  @file character-encoding.cpp
 *  @brief 文字コード処理関数
 */

#include "locale/character-encoding.h"
#include <range/v3/algorithm.hpp>
#include <cstring>

#ifdef JP
#include "system/angband.h"
#include "view/display-messages.h"
#include <algorithm>
#include <vector>

#if defined(__ANDROID__) || defined(ANDROID)
#include <jni.h>
#include <SDL.h>
#endif

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <array>
#include <iconv.h>
#include <utility>
#endif
#endif

/*!
 * @brief 文字列の最初の文字のUTF-8エンコーディングにおけるバイト長を返す
 */
int utf8_next_char_byte_length(std::string_view str)
{
    if (str.empty()) {
        return 0;
    }

    const auto start_byte = static_cast<unsigned char>(str.front());
    size_t length = 0;

    if (start_byte <= 0x7f) {
        return 1;
    } else if ((start_byte & 0xe0) == 0xc0) {
        length = 2;
    } else if ((start_byte & 0xf0) == 0xe0) {
        length = 3;
    } else if ((start_byte & 0xf8) == 0xf0) {
        length = 4;
    } else {
        return 0;
    }

    if (str.size() < length) {
        return 0;
    }

    const auto trailing_bytes = str.substr(1, length - 1);
    const auto is_valid_trailing_byte = [](unsigned char c) { return (c & 0xc0) == 0x80; };

    return ranges::all_of(trailing_bytes, is_valid_trailing_byte) ? length : 0;
}

/*!
 * @brief 文字列がUTF-8の文字列として適合かどうかを判定する
 */
bool is_utf8_str(std::string_view str)
{
    while (!str.empty()) {
        const int byte_length = utf8_next_char_byte_length(str);

        if (byte_length == 0) {
            return false;
        }

        str.remove_prefix(byte_length);
    }

    return true;
}

#ifdef JP

/*!
 * @brief 文字コードをSJISからEUCに変換する / Convert SJIS string to EUC string
 */
void sjis2euc(char *str)
{
    int i;
    unsigned char c1, c2;

    int len = strlen(str);

    std::vector<char> tmp(len + 1);

    for (i = 0; i < len; i++) {
        c1 = str[i];
        if (c1 & 0x80) {
            i++;
            c2 = str[i];
            if (c2 >= 0x9f) {
                c1 = c1 * 2 - (c1 >= 0xe0 ? 0xe0 : 0x60);
                c2 += 2;
            } else {
                c1 = c1 * 2 - (c1 >= 0xe0 ? 0xe1 : 0x61);
                c2 += 0x60 + (c2 < 0x7f);
            }
            tmp[i - 1] = c1;
            tmp[i] = c2;
        } else {
            tmp[i] = c1;
        }
    }
    tmp[len] = 0;
    strcpy(str, tmp.data());
}

/*!
 * @brief 文字コードをEUCからSJISに変換する / Convert EUC string to SJIS string
 */
void euc2sjis(char *str)
{
    int i;
    unsigned char c1, c2;

    int len = strlen(str);

    std::vector<char> tmp(len + 1);

    for (i = 0; i < len; i++) {
        c1 = str[i];
        if (c1 & 0x80) {
            i++;
            c2 = str[i];
            if (c1 % 2) {
                c1 = (c1 >> 1) + (c1 < 0xdf ? 0x31 : 0x71);
                c2 -= 0x60 + (c2 < 0xe0);
            } else {
                c1 = (c1 >> 1) + (c1 < 0xdf ? 0x30 : 0x70);
                c2 -= 2;
            }

            tmp[i - 1] = c1;
            tmp[i] = c2;
        } else {
            tmp[i] = c1;
        }
    }
    tmp[len] = 0;
    strcpy(str, tmp.data());
}

/*!
 * @brief strを環境に合った文字コードに変換し、変換前の文字コードを返す
 */
CharacterEncoding codeconv(char *str)
{
    auto encoding = CharacterEncoding::UNKNOWN;
    for (auto i = 0; str[i]; i++) {
        const auto c1 = static_cast<unsigned char>(str[i]);

        if (!(c1 & 0x80)) {
            continue;
        }

        i++;
        const auto c2 = static_cast<unsigned char>(str[i]);

        const auto in_range = [](unsigned char c, unsigned char lo, unsigned char hi) { return (lo <= c) && (c <= hi); };
        const auto is_euc_jp = in_range(c1, 0xa1, 0xf4) && in_range(c2, 0xa1, 0xfe);
        const auto is_cp932 = (in_range(c1, 0x81, 0x9f) || in_range(c1, 0xe0, 0xef)) && (in_range(c2, 0x40, 0x7e) || in_range(c2, 0x80, 0xfc));

        if (is_euc_jp && is_cp932) {
            continue;
        }

        if (is_euc_jp) {
            if (encoding == CharacterEncoding::UNKNOWN || encoding == CharacterEncoding::EUC_JP) {
                encoding = CharacterEncoding::EUC_JP;
                continue;
            }
        } else if (is_cp932) {
            if (encoding == CharacterEncoding::UNKNOWN || encoding == CharacterEncoding::SHIFT_JIS) {
                encoding = CharacterEncoding::SHIFT_JIS;
                continue;
            }
        }

        return CharacterEncoding::UNKNOWN;
    }

    switch (encoding) {
#ifdef EUC
    case CharacterEncoding::SHIFT_JIS:
        sjis2euc(str);
        break;
#endif

#ifdef SJIS
    case CharacterEncoding::EUC_JP:
        euc2sjis(str);
        break;
#endif
    default:
        break;
    }

    return encoding;
}

/*!
 * @brief 文字列sのxバイト目が漢字の1バイト目かどうか判定する
 */
bool iskanji2(const char *s, int x)
{
    int i;

    for (i = 0; i < x; i++) {
        if (iskanji(s[i])) {
            i++;
        }
    }
    if ((x == i) && iskanji(s[x])) {
        return true;
    }

    return false;
}

/*!
 * @brief 文字列の文字コードがASCIIかどうかを判定する
 */
static bool is_ascii_str(const char *str)
{
    for (; *str; str++) {
        int ch = *str;
        if (!(0x00 < ch && ch <= 0x7f)) {
            return false;
        }
    }
    return true;
}

#ifdef EUC

namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 2> MS_TO_JIS_CHARACTERS = { {
    { "\xef\xbd\x9e", "\xe3\x80\x9c" }, /* FULLWIDTH TILDE -> WAVE DASH */
    { "\xef\xbc\x8d", "\xe2\x88\x92" }, /* FULLWIDTH HYPHEN-MINUS -> MINUS SIGN */
} };
}

static void ms_to_jis_unicode(std::string &str)
{
    for (const auto &[from, to] : MS_TO_JIS_CHARACTERS) {
        for (auto pos = str.find(from); pos != std::string::npos; pos = str.find(from, pos + from.length())) {
            str.replace(pos, from.length(), to);
        }
    }
}

#if defined(__ANDROID__) || defined(ANDROID)
static std::string android_convert_encoding(std::string_view src, const char *from_enc, const char *to_enc)
{
    if (src.empty()) {
        return "";
    }

    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env) {
        return std::string(src);
    }

    jbyteArray jsrc_bytes = env->NewByteArray(static_cast<jsize>(src.size()));
    if (!jsrc_bytes) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return "";
    }
    env->SetByteArrayRegion(jsrc_bytes, 0, static_cast<jsize>(src.size()), reinterpret_cast<const jbyte *>(src.data()));

    jclass str_class = env->FindClass("java/lang/String");
    if (!str_class) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(jsrc_bytes);
        return "";
    }

    jmethodID str_ctor = env->GetMethodID(str_class, "<init>", "([BLjava/lang/String;)V");
    jstring jfrom_enc = env->NewStringUTF(from_enc);
    jstring jstr = static_cast<jstring>(env->NewObject(str_class, str_ctor, jsrc_bytes, jfrom_enc));

    if (env->ExceptionCheck() || !jstr) {
        env->ExceptionClear();
        env->DeleteLocalRef(jsrc_bytes);
        env->DeleteLocalRef(jfrom_enc);
        env->DeleteLocalRef(str_class);
        return "";
    }

    jmethodID get_bytes = env->GetMethodID(str_class, "getBytes", "(Ljava/lang/String;)[B");
    jstring jto_enc = env->NewStringUTF(to_enc);
    jbyteArray jdst_bytes = static_cast<jbyteArray>(env->CallObjectMethod(jstr, get_bytes, jto_enc));

    if (env->ExceptionCheck() || !jdst_bytes) {
        env->ExceptionClear();
        env->DeleteLocalRef(jsrc_bytes);
        env->DeleteLocalRef(jfrom_enc);
        env->DeleteLocalRef(jstr);
        env->DeleteLocalRef(jto_enc);
        env->DeleteLocalRef(str_class);
        return "";
    }

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
 * @brief 文字列の文字コードをUTF-8からEUC-JPに変換する
 */
tl::optional<std::string> utf8_to_euc(std::string_view utf8_str)
{
    if (utf8_str.empty()) {
        return std::string();
    }

    std::string utf8(utf8_str);
    ms_to_jis_unicode(utf8);

#if defined(__ANDROID__) || defined(ANDROID)
    std::string euc = android_convert_encoding(utf8, "UTF-8", "EUC-JP");
    if (euc.empty() && !utf8.empty()) {
        return tl::nullopt;
    }
    return euc;
#else
    static const auto cd = iconv_open("EUC-JP", "UTF-8");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return tl::nullopt;
    }

    std::string euc(utf8.length() * 2, '\0');
    size_t inlen_left = utf8.length();
    size_t outlen_left = euc.length();
    char *in = utf8.data();
    char *out = euc.data();

    if (iconv(cd, &in, &inlen_left, &out, &outlen_left) == (size_t)-1) {
        return tl::nullopt;
    }

    const auto converted_len = euc.length() - outlen_left;
    size_t euc_len = 0;
    for (size_t i = 0; i < converted_len; euc_len++) {
        if (static_cast<unsigned char>(euc[i]) == 0x8f) {
            euc[euc_len] = '?';
            i += 3;
        } else {
            euc[euc_len] = euc[i++];
        }
    }

    euc.resize(euc_len);
    return euc;
#endif
}

/*!
 * @brief 文字列の文字コードをEUC-JPからUTF-8に変換する
 */
int euc_to_utf8(const char *euc_str, size_t euc_str_len, char *utf8_buf, size_t utf8_buf_len)
{
#if defined(__ANDROID__) || defined(ANDROID)
    if (!euc_str || euc_str_len == 0) {
        return 0;
    }
    std::string utf8 = android_convert_encoding(std::string_view(euc_str, euc_str_len), "EUC-JP", "UTF-8");
    if (utf8.empty() && euc_str_len > 0) {
        return -1;
    }
    if (utf8.length() > utf8_buf_len) {
        return -1;
    }
    memcpy(utf8_buf, utf8.data(), utf8.length());
    return static_cast<int>(utf8.length());
#else
    static const auto cd = iconv_open("UTF-8", "EUC-JP");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return -1;
    }

    size_t inlen_left = euc_str_len;
    size_t outlen_left = utf8_buf_len;
    const char *in = euc_str;
    char *out = utf8_buf;

    if (iconv(cd, (char **)&in, &inlen_left, &out, &outlen_left) == (size_t)-1) {
        return -1;
    }

    return utf8_buf_len - outlen_left;
#endif
}
#endif

#if defined(SJIS) && defined(_WIN32)
namespace {
tl::optional<std::string> convert_code_page(std::string_view str, UINT from_code_page, UINT to_code_page)
{
    if (str.empty()) {
        return tl::make_optional<std::string>();
    }

    const auto str_len = static_cast<int>(str.length());
    const auto utf16_len = MultiByteToWideChar(from_code_page, 0, str.data(), str_len, nullptr, 0);
    if (utf16_len == 0) {
        return tl::nullopt;
    }

    std::wstring utf16_str(utf16_len, L'\0');
    MultiByteToWideChar(from_code_page, 0, str.data(), str_len, utf16_str.data(), utf16_len);

    const auto converted_len = WideCharToMultiByte(to_code_page, 0, utf16_str.data(), utf16_len, nullptr, 0, nullptr, nullptr);
    if (converted_len == 0) {
        return tl::nullopt;
    }

    std::string converted(converted_len, '\0');
    WideCharToMultiByte(to_code_page, 0, utf16_str.data(), utf16_len, converted.data(), converted_len, nullptr, nullptr);
    return tl::make_optional(std::move(converted));
}
}
#endif

/*!
 * @brief システムの文字コードからUTF-8に変換する
 */
tl::optional<std::string> sys_to_utf8(std::string_view str)
{
#if defined(EUC)
    std::string utf8str(str.length() * 2 + 1, '\0');
    const auto len = euc_to_utf8(str.data(), str.length(), utf8str.data(), utf8str.size());

    return (len >= 0) ? tl::make_optional(std::move(utf8str.erase(len))) : tl::nullopt;
#elif defined(SJIS) && defined(_WIN32)
    return convert_code_page(str, 932, CP_UTF8);
#else
    return tl::nullopt;
#endif
}

/*!
 * @brief UTF-8からシステムの文字コードに変換する
 */
tl::optional<std::string> utf8_to_sys(std::string_view str)
{
    if (str.find('\0') != std::string_view::npos) {
        return tl::nullopt;
    }

#if defined(EUC)
    return utf8_to_euc(str);
#elif defined(SJIS) && defined(_WIN32)
    return convert_code_page(str, CP_UTF8, 932);
#else
    return tl::nullopt;
#endif
}

/*!
 * @brief 受け取った文字列の文字コードを推定し、システムの文字コードへ変換する
 */
size_t guess_convert_to_system_encoding(char *strbuf, int buflen)
{
    if (is_ascii_str(strbuf)) {
        return std::string_view(strbuf).length();
    }

    if (is_utf8_str(strbuf)) {
        const auto sys_str = utf8_to_sys(strbuf);
        if (!sys_str || std::ssize(*sys_str) >= buflen) {
            msg_print("警告:文字コードの変換に失敗しました");
            msg_erase();
            return std::string_view(strbuf).length();
        }

        std::copy(sys_str->begin(), sys_str->end(), strbuf);
        strbuf[sys_str->length()] = '\0';
        return sys_str->length();
    }

    return std::string_view(strbuf).length();
}

#endif /* JP */
