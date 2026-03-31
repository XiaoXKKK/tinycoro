#pragma once
#include "tinycoro/buffer.h"
#include <string>
#include <unordered_map>

namespace tinycoro {

// Minimal HTTP/1.1 request parser (state-machine, no heap allocations in hot path).
// Usage:
//   HttpParser parser;
//   while (more_data) {
//     auto result = parser.parse(buffer);
//     if (result == HttpParser::COMPLETE) { use parser.request(); parser.reset(); }
//     else if (result == HttpParser::ERROR) { close_conn(); }
//   }

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class HttpParser {
public:
    enum Result { INCOMPLETE, COMPLETE, ERROR };

    HttpParser() = default;

    // Feed bytes from buffer; consumes parsed bytes from buffer.
    Result parse(Buffer& buf);

    void reset();

    const HttpRequest& request() const { return req_; }

private:
    enum class State { REQUEST_LINE, HEADERS, BODY, DONE };

    Result parse_request_line(Buffer& buf);
    Result parse_headers(Buffer& buf);
    Result parse_body(Buffer& buf);

    State state_{State::REQUEST_LINE};
    HttpRequest req_;
    std::size_t content_length_{0};
};

} // namespace tinycoro
