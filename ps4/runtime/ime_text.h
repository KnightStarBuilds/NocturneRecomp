#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nocturne::ps4 {
// CInputString reserves its last byte for NUL and caps its storage at 299.
inline constexpr std::size_t ime_input_capacity(int requested) {
    return requested < 2 ? 0 : (requested > 299 ? 299 : requested);
}

// The recovered UI consumes single-byte printable ASCII. Validate the complete
// UTF-16 result before modifying the caller's buffer; never split or truncate it.
inline bool ime_decode_ascii(const std::uint16_t* text, std::size_t units,
                             char* output, std::size_t capacity) {
    std::size_t length = 0;
    while (length < units && text[length] != 0) {
        if (text[length] < 0x20 || text[length] > 0x7e) return false;
        ++length;
    }
    if (length == units || length >= capacity) return false;
    for (std::size_t i = 0; i < length; ++i) output[i] = static_cast<char>(text[i]);
    output[length] = '\0';
    return true;
}

inline bool ime_complete_filename(char* text, std::size_t capacity,
                                  const char* extension) {
    const auto length = std::strlen(text);
    if (!length || !std::strcmp(text, ".") || !std::strcmp(text, "..")) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c <= 0x20 || c > 0x7e || c == '/' || c == '\\' || c == ':') return false;
    }
    const bool append = std::strchr(text, '.') == nullptr && extension && *extension;
    const std::size_t dot = append && *extension != '.' ? 1 : 0;
    const auto suffix = append ? std::strlen(extension) : 0;
    if (length + dot + suffix >= capacity) return false;
    for (std::size_t i = 0; i < length; ++i)
        if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 'a' - 'A';
    if (dot) text[length] = '.';
    if (append) std::memcpy(text + length + dot, extension, suffix + 1);
    return true;
}
}
