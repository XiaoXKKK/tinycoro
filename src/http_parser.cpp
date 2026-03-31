#include "tinycoro/http_parser.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace tinycoro {

static std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void HttpParser::reset() {
    state_ = State::REQUEST_LINE;
    req_ = {};
    content_length_ = 0;
}

HttpParser::Result HttpParser::parse(Buffer& buf) {
    Result r = INCOMPLETE;
    while (r == INCOMPLETE) {
        switch (state_) {
        case State::REQUEST_LINE: r = parse_request_line(buf); break;
        case State::HEADERS:      r = parse_headers(buf);      break;
        case State::BODY:         r = parse_body(buf);         break;
        case State::DONE:         return COMPLETE;
        }
    }
    return r;
}

HttpParser::Result HttpParser::parse_request_line(Buffer& buf) {
    std::size_t pos = buf.find_crlf();
    if (pos == std::string::npos) return INCOMPLETE;

    std::string line(buf.read_ptr(), pos);
    buf.consume(pos + 2); // consume line + CRLF

    std::istringstream ss(line);
    if (!(ss >> req_.method >> req_.path >> req_.version)) return ERROR;

    state_ = State::HEADERS;
    return INCOMPLETE;
}

HttpParser::Result HttpParser::parse_headers(Buffer& buf) {
    for (;;) {
        std::size_t pos = buf.find_crlf();
        if (pos == std::string::npos) return INCOMPLETE;

        if (pos == 0) {
            // Empty line → end of headers
            buf.consume(2);
            auto it = req_.headers.find("Content-Length");
            if (it != req_.headers.end()) {
                content_length_ = std::stoul(it->second);
            }
            state_ = (content_length_ > 0) ? State::BODY : State::DONE;
            return INCOMPLETE;
        }

        std::string line(buf.read_ptr(), pos);
        buf.consume(pos + 2);

        std::size_t colon = line.find(':');
        if (colon == std::string::npos) return ERROR;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        req_.headers[key] = val;
    }
}

HttpParser::Result HttpParser::parse_body(Buffer& buf) {
    if (buf.readable() < content_length_) return INCOMPLETE;
    req_.body.assign(buf.read_ptr(), content_length_);
    buf.consume(content_length_);
    state_ = State::DONE;
    return COMPLETE;
}

} // namespace tinycoro
