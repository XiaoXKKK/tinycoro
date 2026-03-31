#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace tinycoro {

// Ring-buffer read/write buffer.
// Avoids data copying on sequential reads by tracking head/tail positions.
class Buffer {
public:
    static constexpr std::size_t kInitialSize = 4096;

    explicit Buffer(std::size_t initial = kInitialSize)
        : buf_(initial), read_idx_(0), write_idx_(0) {}

    // Readable bytes
    std::size_t readable() const { return write_idx_ - read_idx_; }

    // Writable bytes remaining without reallocation
    std::size_t writable() const { return buf_.size() - write_idx_; }

    const char* read_ptr() const { return buf_.data() + read_idx_; }
    char* write_ptr()            { return buf_.data() + write_idx_; }

    // Append raw bytes into the write area; grows if needed
    void append(const char* data, std::size_t len) {
        ensure_writable(len);
        std::memcpy(write_ptr(), data, len);
        write_idx_ += len;
    }

    void append(const std::string& s) { append(s.data(), s.size()); }

    // Consume n bytes from read side
    void consume(std::size_t n) {
        read_idx_ += n;
        if (read_idx_ == write_idx_) {
            // Reset to front of buffer to avoid perpetual growth
            read_idx_ = write_idx_ = 0;
        }
    }

    // Retrieve all readable data as a string
    std::string retrieve_all_as_string() {
        std::string s(read_ptr(), readable());
        consume(readable());
        return s;
    }

    // Find "\r\n" in readable region; returns offset or npos
    std::size_t find_crlf() const {
        const char* p = read_ptr();
        std::size_t n = readable();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            if (p[i] == '\r' && p[i+1] == '\n') return i;
        }
        return std::string::npos;
    }

    void ensure_writable(std::size_t len) {
        if (writable() >= len) return;
        // Try compacting first
        if (read_idx_ + writable() >= len) {
            std::size_t r = readable();
            std::memmove(buf_.data(), read_ptr(), r);
            read_idx_ = 0;
            write_idx_ = r;
        } else {
            buf_.resize(write_idx_ + len);
        }
    }

private:
    std::vector<char> buf_;
    std::size_t read_idx_;
    std::size_t write_idx_;
};

} // namespace tinycoro
