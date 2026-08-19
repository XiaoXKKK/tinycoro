#include <tinycoro/buffer.h>

int main() {
    tinycoro::Buffer buffer;
    buffer.append("package-ok");
    return buffer.retrieve_all_as_string() == "package-ok" ? 0 : 1;
}