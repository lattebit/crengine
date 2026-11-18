/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：HarfBuzz 使用引擎预算和显式关闭；隔离 Picolibc 宏，第三方源保持原样。
 * English: Budget HarfBuzz allocations and release on shutdown; isolate Picolibc macros without changing vendor sources.
 */
#include <locale>
#include <cstring>
#include <limits>
#include <cstdlib>
#include "../include/cre_mcu_memory.h"
namespace {
struct alignas(max_align_t) Allocation { size_t bytes; };
using Cleanup = void (*)();
Cleanup cleanups[32]{};
size_t cleanup_count = 0;
void on_shutdown(Cleanup cleanup) {
    for (size_t i = 0; i < cleanup_count; ++i) if (cleanups[i] == cleanup) return;
    // 中文：当前裁剪仅需少量静态回调；依赖升级超限必须显式处理。
    // English: This trimmed build needs few static callbacks; dependency upgrades must handle overflow explicitly.
    if (cleanup_count == sizeof(cleanups) / sizeof(*cleanups)) std::abort();
    cleanups[cleanup_count++] = cleanup;
}
}
#define hb_malloc_impl cre_hb_malloc
#define hb_calloc_impl cre_hb_calloc
#define hb_realloc_impl cre_hb_realloc
#define hb_free_impl cre_hb_free
#define hb_atexit(f) on_shutdown(f)
#include "hb.hh"
extern "C" void *cre_hb_malloc(size_t bytes) {
    if (bytes > std::numeric_limits<size_t>::max() - sizeof(Allocation)) return cre_mcu_allocate(std::numeric_limits<size_t>::max());
    auto *p = static_cast<Allocation *>(cre_mcu_allocate(sizeof(Allocation) + bytes));
    if (!p) return nullptr;
    p->bytes = bytes;
    return p + 1;
}
extern "C" void cre_hb_free(void *data) {
    if (data) cre_mcu_release(static_cast<Allocation *>(data) - 1);
}
extern "C" void *cre_hb_calloc(size_t count, size_t bytes) {
    if (bytes && count > std::numeric_limits<size_t>::max() / bytes) return cre_mcu_allocate(std::numeric_limits<size_t>::max());
    void *p = cre_hb_malloc(count * bytes);
    if (p) std::memset(p, 0, count * bytes);
    return p;
}
extern "C" void *cre_hb_realloc(void *data, size_t bytes) {
    if (!bytes) { cre_hb_free(data); return nullptr; }
    void *p = cre_hb_malloc(bytes);
    if (!p) return nullptr;
    if (data) {
        auto *old = static_cast<Allocation *>(data) - 1;
        std::memcpy(p, data, bytes < old->bytes ? bytes : old->bytes);
        cre_hb_free(data);
    }
    return p;
}
void cre_mcu_harfbuzz_shutdown() {
    while (cleanup_count) cleanups[--cleanup_count]();
}
#pragma push_macro("_S")
#undef _S
#include "harfbuzz_mcu.cc"
#pragma pop_macro("_S")
