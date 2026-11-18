/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：对真实文档打开和绘制注入 C++ 分配失败，检查错误、释放与正常重开。
 * English: Inject C++ allocation failures into real document open/draw and check errors, cleanup and successful reopen.
 */
#include "platform.h"
#include "cre_mcu_document.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <vector>
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif

namespace {
size_t live_bytes = 0, calls = 0, fail_at = SIZE_MAX;
bool failed = false;
bool persistent_failure = false;
union Header { std::max_align_t alignment; size_t bytes; };
}
void *operator new(size_t bytes) {
    size_t call = calls++;
    if (call == fail_at || (persistent_failure && call > fail_at)) {
#if defined(__linux__) || defined(__APPLE__)
        if (std::getenv("CRE_TEST_ALLOCATION_TRACE")) {
            void *frames[32]; int count = backtrace(frames, 32);
            backtrace_symbols_fd(frames, count, 2);
        }
#endif
        failed = true; throw std::bad_alloc();
    }
    if (bytes > SIZE_MAX - sizeof(Header)) throw std::bad_alloc();
    auto *block = static_cast<Header *>(std::malloc(sizeof(Header) + std::max(size_t(1), bytes)));
    if (!block) throw std::bad_alloc();
    block->bytes = bytes; live_bytes += bytes; return block + 1;
}
void operator delete(void *data) noexcept {
    if (!data) return;
    auto *block = static_cast<Header *>(data) - 1; assert(live_bytes >= block->bytes);
    live_bytes -= block->bytes; std::free(block);
}
void *operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *data) noexcept { ::operator delete(data); }
void *operator new(size_t bytes, const std::nothrow_t &) noexcept { try { return ::operator new(bytes); } catch (...) { return nullptr; } }
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept { return ::operator new(bytes, std::nothrow); }
void operator delete(void *data, const std::nothrow_t &) noexcept { ::operator delete(data); }
void operator delete[](void *data, const std::nothrow_t &) noexcept { ::operator delete(data); }
void operator delete(void *data, size_t) noexcept { ::operator delete(data); }
void operator delete[](void *data, size_t) noexcept { ::operator delete(data); }

int main(int argc, char **argv) {
#if defined(__linux__) || defined(__APPLE__)
    std::set_terminate([] { void *frames[40]; int count = backtrace(frames, 40); backtrace_symbols_fd(frames, count, 2); std::abort(); });
#endif
    assert(argc == 3 || argc == 4 || (argc == 6 && !std::strcmp(argv[3], "--case")));
    auto path = std::string(argv[2]) + "/fixture.html";
    cre_layout layout{320, 480, 24, 400, 8, 100, 0, 0, 0, {}, 8};
    assert(std::strlen(argv[1]) < sizeof(layout.font)); std::strcpy(layout.font, argv[1]);
    std::vector<uint8_t> pixels(320 * 480 / 2), expected;
    auto run = [&](size_t fault, bool drawing) {
        if (fault != SIZE_MAX && std::getenv("CRE_TEST_ALLOCATION_TRACE"))
            std::fprintf(stderr, "probe=%zu drawing=%d persistent=%d\n", fault, drawing, persistent_failure);
        Platform platform; auto cfg = config(platform); cfg.memory_limit = 6 * 1024 * 1024;
        assert(cre_mcu_initialize(&cfg) == CRE_OK);
        cre_document *document = nullptr; cre_page_state state{}; cre_anchor anchor{};
        int result = CRE_OK; failed = false;
        if (drawing) assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        size_t first_call = calls;
        fail_at = fault == SIZE_MAX ? SIZE_MAX : calls + fault;
        if (!drawing) result = cre_document_open(path.c_str(), &layout, &document);
        if (result == CRE_OK) result = cre_document_prepare(document, &anchor, 0, pixels.data(), pixels.size(), &state);
        size_t operation_calls = calls - first_call; fail_at = SIZE_MAX;
        if (failed && result != CRE_MEMORY) std::fprintf(stderr, "fault=%zu draw=%d result=%d\n", fault, drawing, result);
        assert(!failed || result == CRE_MEMORY);
        if (!failed) { assert(result == CRE_OK); if (expected.empty()) expected = pixels; else assert(expected == pixels); }
        cre_document_close(document);
        // 中文：失败后的重开复用同一个引擎，不能依赖全局关闭来清理半注册字体。
        // English: Reopen in the same engine after failure, without relying on global shutdown to clear partial fonts.
        if (fault != SIZE_MAX) {
            document = nullptr;
            assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
            assert(cre_document_prepare(document, &anchor, 0, pixels.data(), pixels.size(), &state) == CRE_OK);
            assert(expected == pixels);
            cre_document_close(document);
        }
        cre_mcu_shutdown();
        assert(!platform.files && !platform.live);
        return operation_calls;
    };
    const size_t operation_calls[] = {run(SIZE_MAX, false), run(SIZE_MAX, true)};
    size_t baseline = live_bytes;
    auto checkLive = [&] {
#if defined(__SANITIZE_ADDRESS__)
        if (live_bytes != baseline) __lsan_do_leak_check();
#endif
        assert(live_bytes == baseline);
    };
    if (argc == 6) {
        run(std::strtoull(argv[4], nullptr, 10), std::atoi(argv[5]) != 0);
        checkLive(); return 0;
    }
    unsigned count = argc == 4 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 100;
    for (bool drawing : {false, true}) for (unsigned probe = 0; probe < count + (count ? 64 : 0); ++probe) {
        size_t fault = probe < count ? probe : operation_calls[drawing] * (probe - count + 1) / 65;
        if (!(probe % 25)) std::fprintf(stderr, "C++ allocation fault=%zu draw=%d\n", fault, drawing);
        run(fault, drawing);
        if (live_bytes != baseline) std::fprintf(stderr, "fault=%zu draw=%d C++ bytes baseline=%zu actual=%zu\n", fault, drawing, baseline, live_bytes);
        checkLive();
    }
    persistent_failure = true;
    for (bool drawing : {false, true}) for (unsigned probe = 0; probe < 64; ++probe) {
        size_t fault = probe < 32 ? probe : operation_calls[drawing] * (probe - 31) / 33;
        if (!(probe % 16)) std::fprintf(stderr, "Persistent C++ allocation fault=%zu draw=%d\n", fault, drawing);
        run(fault, drawing); checkLive();
    }
    std::puts("C++ document allocation failures return typed errors, release state and preserve clean reread pixels");
}
