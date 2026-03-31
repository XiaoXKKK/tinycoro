#include "tinycoro/buffer.h"
#include "tinycoro/http_parser.h"
#include <gtest/gtest.h>
#include <string>

using namespace tinycoro;

// ---- Buffer ------------------------------------------------------------

TEST(BufferTest, AppendAndRead) {
    Buffer buf;
    buf.append("hello", 5);
    EXPECT_EQ(buf.readable(), 5u);
    EXPECT_EQ(std::string(buf.read_ptr(), buf.readable()), "hello");
}

TEST(BufferTest, ConsumeResetsOnEmpty) {
    Buffer buf;
    buf.append("hi", 2);
    buf.consume(2);
    EXPECT_EQ(buf.readable(), 0u);
    // After full consume, internal pointers reset to 0 — new appends go to front
    buf.append("world", 5);
    EXPECT_EQ(buf.readable(), 5u);
}

TEST(BufferTest, RetrieveAllAsString) {
    Buffer buf;
    buf.append("abc", 3);
    buf.append("def", 3);
    std::string s = buf.retrieve_all_as_string();
    EXPECT_EQ(s, "abcdef");
    EXPECT_EQ(buf.readable(), 0u);
}

TEST(BufferTest, FindCRLF) {
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\n", 16);
    EXPECT_EQ(buf.find_crlf(), 14u);
}

TEST(BufferTest, GrowsOnLargeAppend) {
    Buffer buf(16); // small initial size
    std::string big(1024, 'x');
    buf.append(big);
    EXPECT_EQ(buf.readable(), 1024u);
}

// ---- HttpParser --------------------------------------------------------

TEST(HttpParserTest, SimpleGET) {
    HttpParser parser;
    Buffer buf;
    std::string req =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    buf.append(req);

    auto result = parser.parse(buf);
    EXPECT_EQ(result, HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().method, "GET");
    EXPECT_EQ(parser.request().path, "/hello");
    EXPECT_EQ(parser.request().version, "HTTP/1.1");
    EXPECT_EQ(parser.request().headers.at("Host"), "localhost");
}

TEST(HttpParserTest, POSTWithBody) {
    HttpParser parser;
    Buffer buf;
    std::string body = "name=world";
    std::string req =
        "POST /echo HTTP/1.1\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;
    buf.append(req);

    auto result = parser.parse(buf);
    EXPECT_EQ(result, HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().method, "POST");
    EXPECT_EQ(parser.request().body, body);
}

TEST(HttpParserTest, IncompleteRequest) {
    HttpParser parser;
    Buffer buf;
    // Only the request line, no headers yet
    buf.append("GET / HTTP/1.1\r\n");
    auto result = parser.parse(buf);
    EXPECT_EQ(result, HttpParser::INCOMPLETE);
}

TEST(HttpParserTest, ResetAndReuseKeepAlive) {
    HttpParser parser;
    Buffer buf;

    auto make_get = [](const std::string& path) {
        return "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n";
    };

    buf.append(make_get("/first"));
    EXPECT_EQ(parser.parse(buf), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().path, "/first");

    parser.reset();
    buf.append(make_get("/second"));
    EXPECT_EQ(parser.parse(buf), HttpParser::COMPLETE);
    EXPECT_EQ(parser.request().path, "/second");
}
