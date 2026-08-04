#pragma once

#include <array>
#include <cstddef>
#include <openssl/sha.h>
#include <string>

// 计算字符串的 SHA-256，并返回固定长度的 64 字符十六进制结果。
inline std::string sha256_hex(const std::string& input) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), digest.data());

    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    std::string result(digest.size() * 2, '\0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = HEX_DIGITS[(digest[i] >> 4) & 0x0F];
        result[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0F];
    }
    return result;
}

