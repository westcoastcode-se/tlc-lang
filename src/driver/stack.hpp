// Runs the compiler on a thread with a large stack.
#pragma once

#include <cstddef>
#include <type_traits>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace tlc {

// The checker, the folds, and codegen walk the tree recursively. A left-deep `a + b + ...` chain is one
// level per link, about 2 KB each in a debug build, and the default 8 MB stack held about 4000 links. The
// parser allows `kMaxChain` links, and this stack holds them in every phase with room to spare. The pages
// are reserved, not committed, so a small program uses no more memory than before.
inline constexpr std::size_t kCompilerStack = std::size_t{1} << 30;

// Calls `body` on a new thread with `kCompilerStack` bytes of stack and returns its result. Where the
// thread cannot be made, `body` runs on the current thread.
template <class Body>
int run_on_large_stack(Body &&body) {
    using Fn = std::remove_reference_t<Body>;
    struct Call {
        Fn *body;
        int result = 1;
    } call{&body};
#if defined(_WIN32)
    const auto entry = [](LPVOID raw) -> DWORD {
        auto *self = static_cast<Call *>(raw);
        self->result = (*self->body)();
        return 0;
    };
    HANDLE thread = CreateThread(nullptr, kCompilerStack, entry, &call, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (thread == nullptr) {
        return body();
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return call.result;
#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        return body();
    }
    pthread_t thread;
    const auto entry = [](void *raw) -> void * {
        auto *self = static_cast<Call *>(raw);
        self->result = (*self->body)();
        return nullptr;
    };
    const bool made = pthread_attr_setstacksize(&attr, kCompilerStack) == 0 &&
                      pthread_create(&thread, &attr, entry, &call) == 0;
    pthread_attr_destroy(&attr);
    if (!made) {
        return body();
    }
    pthread_join(thread, nullptr);
    return call.result;
#endif
}

} // namespace tlc
