#include "text_stream.h"
#include <string>
#include <sys/types.h>

#if !defined(_WIN32)
namespace {
#if defined(__APPLE__)
using ReadSize = int;
using ReadResult = int;
#else
using ReadSize = size_t;
using ReadResult = ssize_t;
#endif
ReadResult read_text(void* cookie, char* buf, ReadSize size) {
    auto* source = static_cast<FILE*>(cookie);
    ReadSize count = 0;
    while (count < size) {
        const int c = fgetc(source);
        if (c == EOF) break;
        if (c != '\r') buf[count++] = static_cast<char>(c);
    }
    return count == 0 && ferror(source) ? -1 : static_cast<ReadResult>(count);
}
ReadResult write_text(void* cookie, const char* buf, ReadSize size) {
    auto* source = static_cast<FILE*>(cookie);
    const auto count = fwrite(buf, 1, size, source);
    return count == 0 && ferror(source) ? -1 : static_cast<ReadResult>(count);
}
#if defined(__APPLE__)
fpos_t seek_text(void* cookie, fpos_t offset, int whence) {
    auto* source = static_cast<FILE*>(cookie);
    if (fseeko(source, offset, whence) != 0) return -1;
    return ftello(source);
}
#else
int seek_text(void* cookie, off64_t* offset, int whence) {
    auto* source = static_cast<FILE*>(cookie);
    if (fseeko(source, *offset, whence) != 0) return -1;
    *offset = ftello(source);
    return 0;
}
#endif
int close_text(void* cookie) { return fclose(static_cast<FILE*>(cookie)); }
}
#endif

FILE* nocturne_fopen_text(const char* path, const char* mode) {
#if defined(_WIN32)
    return fopen(path, mode);
#else
    const std::string flags(mode);
    if (flags.find('b') != std::string::npos) return fopen(path, mode);
    std::string effective;
    for (char c : flags) if (c != 't') effective += c;
    FILE* source = fopen(path, effective.c_str());
    if (!source) return nullptr;
    const bool readable = flags.front() == 'r' || flags.find('+') != std::string::npos;
    const bool writable = flags.front() != 'r' || flags.find('+') != std::string::npos;
#if defined(__APPLE__)
    FILE* stream = funopen(source, readable ? read_text : nullptr,
                          writable ? write_text : nullptr, seek_text, close_text);
#else
    cookie_io_functions_t ops{readable ? read_text : nullptr,
                             writable ? write_text : nullptr, seek_text, close_text};
    FILE* stream = fopencookie(source, effective.c_str(), ops);
#endif
    if (!stream) { fclose(source); return nullptr; }
    // A second buffer would express seek deltas in translated rather than physical
    // bytes. POD offsets and ftell/fseek must keep using the underlying file.
    setvbuf(stream, nullptr, _IONBF, 0);
    return stream;
#endif
}
