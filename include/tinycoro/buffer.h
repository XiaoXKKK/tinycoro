#pragma once
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace tinycoro {

// Contiguous read/write buffer with independent read and write cursors.
// It compacts or grows only when the tail has insufficient writable space.
class Buffer {
  public:
    static constexpr std::size_t kInitialSize = 4096;

    explicit Buffer(std::size_t initial = kInitialSize)
        : buf_(initial), read_idx_(0), write_idx_(0) {}

    std::size_t readable() const { return write_idx_ - read_idx_; }
    std::size_t writable() const { return buf_.size() - write_idx_; }

    const char* read_ptr() const { return buf_.data() + read_idx_; }
    char* write_ptr() { return buf_.data() + write_idx_; }

    // Commit bytes written directly into write_ptr().
    void has_written(std::size_t n) {
        if (n > writable())
            throw std::out_of_range("Buffer::has_written");
        write_idx_ += n;
    }

    void append(const char* data, std::size_t len) {
        ensure_writable(len);
        std::memcpy(write_ptr(), data, len);
        write_idx_ += len;
    }

    void append(const std::string& data) { append(data.data(), data.size()); }

    void consume(std::size_t n) {
        if (n > readable())
            throw std::out_of_range("Buffer::consume");
        read_idx_ += n;
        if (read_idx_ == write_idx_) {
            read_idx_ = write_idx_ = 0;
        }
    }

    std::string retrieve_all_as_string() {
        std::string result(read_ptr(), readable());
        consume(readable());
        return result;
    }

    std::size_t find_crlf() const {
        const char* data = read_ptr();
        const std::size_t size = readable();
        for (std::size_t i = 0; i + 1 < size; ++i) {
            if (data[i] == '\r' && data[i + 1] == '\n')
                return i;
        }
        return std::string::npos;
    }

    void ensure_writable(std::size_t len) {
        if (writable() >= len)
            return;
        if (read_idx_ + writable() >= len) {
            const std::size_t size = readable();
            std::memmove(buf_.data(), read_ptr(), size);
            read_idx_ = 0;
            write_idx_ = size;
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