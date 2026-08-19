#include "tinycoro/http_parser.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <string_view>

namespace tinycoro {
namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool iequals(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
           });
}

bool parse_size(std::string_view value, std::size_t& result) {
    if (value.empty())
        return false;
    std::size_t parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size()) {
        return false;
    }
    result = parsed;
    return true;
}

} // namespace

HttpParser::HttpParser() : HttpParser(Limits{}) {
}

HttpParser::HttpParser(Limits limits) : limits_(limits) {
}

void HttpParser::reset() {
    state_ = State::REQUEST_LINE;
    request_ = {};
    content_length_ = 0;
    header_bytes_ = 0;
    content_length_seen_ = false;
}

HttpParser::Result HttpParser::parse(Buffer& buffer) {
    for (;;) {
        const State before = state_;
        Result result = INCOMPLETE;
        switch (state_) {
        case State::REQUEST_LINE:
            result = parse_request_line(buffer);
            break;
        case State::HEADERS:
            result = parse_headers(buffer);
            break;
        case State::BODY:
            result = parse_body(buffer);
            break;
        case State::DONE:
            return COMPLETE;
        }
        if (result == ERROR || result == COMPLETE)
            return result;
        if (state_ == before)
            return INCOMPLETE;
    }
}

HttpParser::Result HttpParser::parse_request_line(Buffer& buffer) {
    const std::size_t end = buffer.find_crlf();
    if (end == std::string::npos) {
        return buffer.readable() > limits_.request_line_bytes ? ERROR : INCOMPLETE;
    }
    if (end > limits_.request_line_bytes)
        return ERROR;

    const std::string line(buffer.read_ptr(), end);
    buffer.consume(end + 2);

    std::istringstream input(line);
    std::string extra;
    if (!(input >> request_.method >> request_.path >> request_.version) || (input >> extra)) {
        return ERROR;
    }
    if (request_.version != "HTTP/1.1" && request_.version != "HTTP/1.0") {
        return ERROR;
    }

    state_ = State::HEADERS;
    return INCOMPLETE;
}

HttpParser::Result HttpParser::parse_headers(Buffer& buffer) {
    for (;;) {
        const std::size_t end = buffer.find_crlf();
        if (end == std::string::npos) {
            return header_bytes_ + buffer.readable() > limits_.header_bytes ? ERROR : INCOMPLETE;
        }
        if (header_bytes_ + end + 2 > limits_.header_bytes)
            return ERROR;
        header_bytes_ += end + 2;

        if (end == 0) {
            buffer.consume(2);
            state_ = content_length_ == 0 ? State::DONE : State::BODY;
            return INCOMPLETE;
        }

        const std::string line(buffer.read_ptr(), end);
        buffer.consume(end + 2);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            return ERROR;

        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key.empty())
            return ERROR;

        if (iequals(key, "Content-Length")) {
            std::size_t parsed = 0;
            if (!parse_size(value, parsed) || parsed > limits_.body_bytes) {
                return ERROR;
            }
            if (content_length_seen_ && parsed != content_length_)
                return ERROR;
            content_length_seen_ = true;
            content_length_ = parsed;
        } else if (iequals(key, "Transfer-Encoding") && !iequals(value, "identity")) {
            return ERROR;
        }
        request_.headers[key] = value;
    }
}

HttpParser::Result HttpParser::parse_body(Buffer& buffer) {
    if (content_length_ > limits_.body_bytes)
        return ERROR;
    if (buffer.readable() < content_length_)
        return INCOMPLETE;
    request_.body.assign(buffer.read_ptr(), content_length_);
    buffer.consume(content_length_);
    state_ = State::DONE;
    return COMPLETE;
}

} // namespace tinycoro