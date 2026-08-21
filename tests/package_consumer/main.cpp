#include <tinycoro/io_context.h>
#include <tinycoro/task.h>

namespace {
tinycoro::Task<void> mark_ran(bool& ran) {
    ran = true;
    co_return;
}
} // namespace

int main() {
    bool ran = false;
    tinycoro::IoContext context;
    context.spawn(mark_ran(ran));
    context.run();
    return ran ? 0 : 1;
}