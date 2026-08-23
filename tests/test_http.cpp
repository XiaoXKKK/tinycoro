#include "tinycoro/buffer.h"
#include "tinycoro/http_parser.h"
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>

using namespace tinycoro;

TEST(BufferTest, AppendAndRead) {
    Buffer buffer;
    buffer.append("hello", 5);
    EXPECT_EQ(buffer.readable(), 5u);
    EXPECT_EQ(std::string(buffer.read_ptr(), buffer.readable()), "hello");
}

TEST(BufferTest, DirectWriteCommit) {
    Buffer buffer(8);
    std::memcpy(buffer.write_ptr(), "direct", 6);
    buffer.has_written(6);
    EXPECT_EQ(buffer.retrieve_all_as_string(), "direct");
}

TEST(BufferTest, ConsumeResetsOnEmpty) {
    Buffer buffer;
    buffer.append("hi", 2);
    buffer.consume(2);
    EXPECT_EQ(buffer.readable(), 0u);
    buffer.append("world", 5);
    EXPECT_EQ(buffer.readable(), 5u);
}

TEST(BufferTest, RejectsOutOfRangeCursorMovement) {
    Buffer buffer(4);
    buffer.append("abc", 3);
    EXPECT_THROW(buffer.consume(4), std::out_of_range);
    EXPECT_THROW(buffer.has_written(2), std::out_of_range);
}

TEST(BufferTest, RetrieveAllAsString) {
    Buffer buffer;
    buffer.append("abc", 3);
    buffer.append("def", 3);
    EXPECT_EQ(buffer.retrieve_all_as_string(), "abcdef");
    EXPECT_EQ(buffer.readable(), 0u);
}

TEST(BufferTest, FindCRLF) {
    Buffer buffer;
    buffer.append("GET / HTTP/1.1\r\n", 16);
    EXPECT_EQ(buffer.find_crlf(), 14u);
}

TEST(BufferTest, GrowsOnLargeAppend) {
    Buffer buffer(16);
    const std::string large(1024, 'x');
    buffer.append(large);
    EXPECT_EQ(buffer.readable(), 1024u);
}

TEST(BufferTest, ZeroInitialCapacityStillSupportsAppend) {
    Buffer buffer(0);
    buffer.append("x", 1);
    EXPECT_EQ(buffer.retrieve_all_as_string(), "x");
}

TEST(BufferTest, RejectsNullInputAndCapacityOverflow) {
    Buffer buffer(4);
    EXPECT_THROW(buffer.append(nullptr, 1), std::invalid_argument);

    buffer.append("x", 1);
    EXPECT_THROW(buffer.ensure_writable(std::numeric_limits<std::size_t>::max()),
                 std::length_error);
}

TEST(HttpParserTest, SimpleGet) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("GET /hello HTTP/1.1\r\n"
                  "Host: localhost\r\n"
                  "Connection: keep-alive\r\n\r\n");

    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().method, "GET");
    EXPECT_EQ(parser.request().path, "/hello");
    EXPECT_EQ(parser.request().version, "HTTP/1.1");
    EXPECT_EQ(parser.request().headers.at("Host"), "localhost");
}

TEST(HttpParserTest, PostBodyMayArriveInFragments) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST /echo HTTP/1.1\r\n"
                  "Content-Length: 10\r\n\r\n"
                  "1234");
    EXPECT_EQ(parser.parse(buffer), HttpParser::INCOMPLETE);
    buffer.append("567890");
    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().body, "1234567890");
}

TEST(HttpParserTest, IncompleteRequestDoesNotSpin) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("GET / HTTP/1.1\r\n");
    EXPECT_EQ(parser.parse(buffer), HttpParser::INCOMPLETE);
}

TEST(HttpParserTest, ResetPreservesPipelinedBytes) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("GET /first HTTP/1.1\r\nHost: x\r\n\r\n"
                  "GET /second HTTP/1.1\r\nHost: x\r\n\r\n");

    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().path, "/first");
    EXPECT_GT(buffer.readable(), 0u);

    parser.reset();
    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().path, "/second");
    EXPECT_EQ(buffer.readable(), 0u);
}

TEST(HttpParserTest, InvalidContentLengthReturnsError) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Content-Length: not-a-number\r\n\r\n");
    EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);
}

TEST(HttpParserTest, ConflictingContentLengthsAreRejected) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Content-Length: 1\r\n"
                  "content-length: 2\r\n\r\n"
                  "xx");
    EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);
}

TEST(HttpParserTest, UnsupportedChunkedEncodingIsRejected) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Transfer-Encoding: chunked\r\n\r\n");
    EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);
}

TEST(HttpParserTest, IdentityTransferEncodingWithContentLengthIsAccepted) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Content-Length: 4\r\n"
                  "Transfer-Encoding: identity\r\n\r\n"
                  "body");

    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().body, "body");
}

TEST(HttpParserTest, ChunkedEncodingWithContentLengthIsRejected) {
    HttpParser parser;
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Content-Length: 4\r\n"
                  "Transfer-Encoding: chunked\r\n\r\n"
                  "body");

    EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);
}

TEST(HttpParserTest, StrictRequestLineRejectsUnsupportedForms) {
    const char* invalid_requests[] = {
        "GET / HTTP/2.0\r\n\r\n",
        "GET / HTTP/1.x\r\n\r\n",
        "GET / HTTP/1.1 EXTRA\r\n\r\n",
    };

    for (const char* request : invalid_requests) {
        SCOPED_TRACE(request);
        HttpParser parser;
        Buffer buffer;
        buffer.append(request);
        EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);
    }
}

TEST(HttpParserTest, ConfiguredLimitsBoundMemoryGrowth) {
    HttpParser::Limits limits;
    limits.request_line_bytes = 16;
    limits.header_bytes = 32;
    limits.body_bytes = 4;
    HttpParser parser(limits);
    Buffer buffer;
    buffer.append("GET /this-path-is-too-long");
    EXPECT_EQ(parser.parse(buffer), HttpParser::ERROR);

    parser.reset();
    Buffer body;
    body.append("POST / HTTP/1.1\r\n"
                "Content-Length: 5\r\n\r\n"
                "12345");
    EXPECT_EQ(parser.parse(body), HttpParser::ERROR);
}

TEST(HttpParserTest, ConfiguredLimitsAcceptExactBoundaries) {
    HttpParser::Limits limits;
    limits.request_line_bytes = 15;
    limits.header_bytes = 21;
    limits.body_bytes = 4;
    HttpParser parser(limits);
    Buffer buffer;
    buffer.append("POST / HTTP/1.1\r\n"
                  "Content-Length: 4\r\n\r\n"
                  "body");

    EXPECT_EQ(parser.parse(buffer), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().body, "body");
}
