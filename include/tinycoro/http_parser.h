#pragma once
#include "tinycoro/buffer.h"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace tinycoro {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

// Incremental HTTP/1.1 request parser for the demo server. It deliberately
// supports Content-Length bodies only; chunked transfer encoding is rejected.
class HttpParser {
  public:
    enum Result { INCOMPLETE, COMPLETE, ERROR };

    struct Limits {
        std::size_t request_line_bytes{8 * 1024};
        std::size_t header_bytes{64 * 1024};
        std::size_t body_bytes{1024 * 1024};
    };

    HttpParser();
    explicit HttpParser(Limits limits);

    // Consumes only bytes belonging to the current request. Pipelined bytes
    // remain in the Buffer for the next reset()/parse() cycle.
    Result parse(Buffer& buffer);
    void reset();

    const HttpRequest& request() const { return request_; }

  private:
    enum class State { REQUEST_LINE, HEADERS, BODY, DONE };

    Result parse_request_line(Buffer& buffer);
    Result parse_headers(Buffer& buffer);
    Result parse_body(Buffer& buffer);

    Limits limits_;
    State state_{State::REQUEST_LINE};
    HttpRequest request_;
    std::size_t content_length_{0};
    std::size_t header_bytes_{0};
    bool content_length_seen_{false};
};

} // namespace tinycoro