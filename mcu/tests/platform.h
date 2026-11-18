/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：真实引擎测试使用的文件与故障注入平台。
 * English: File and fault-injection platform for real engine tests.
 */
#pragma once
#include "cre_mcu.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
struct Platform {
    size_t allocations = 0, live = 0, fail_at = SIZE_MAX, files = 0;
    bool cancel = false, fail_read = false;
    size_t checkpoints = 0, cancel_at = SIZE_MAX;
    size_t reads = 0, fail_read_at = SIZE_MAX;
};
static void *allocate(void *context, size_t bytes) {
    auto &p = *static_cast<Platform *>(context);
    if (p.allocations++ == p.fail_at) return nullptr;
    void *result = std::malloc(bytes); if (result) ++p.live; return result;
}
static void release(void *context, void *data) {
    if (data) { auto &p = *static_cast<Platform *>(context); assert(p.live); --p.live; std::free(data); }
}
static void *openFile(void *context, const char *path, uint64_t *bytes) {
    FILE *f = std::fopen(path, "rb"); if (!f) return nullptr;
    if (std::fseek(f, 0, SEEK_END)) { std::fclose(f); return nullptr; }
    long size = std::ftell(f); if (size < 0) { std::fclose(f); return nullptr; }
    *bytes = static_cast<uint64_t>(size); ++static_cast<Platform *>(context)->files; return f;
}
static size_t readAt(void *context, void *file, uint64_t offset, void *data, size_t bytes) {
    auto &p = *static_cast<Platform *>(context);
    if (p.fail_read || p.reads++ >= p.fail_read_at) return 0;
    FILE *f = static_cast<FILE *>(file);
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET)) return 0;
    return std::fread(data, 1, bytes, f);
}
static void closeFile(void *context, void *file) {
    auto &p = *static_cast<Platform *>(context); assert(p.files); --p.files; std::fclose(static_cast<FILE *>(file));
}
static int cancelled(void *context) { auto &p = *static_cast<Platform *>(context); return p.cancel || p.checkpoints++ >= p.cancel_at; }
static cre_mcu_platform_t config(Platform &p) {
    return {&p, allocate, release, openFile, readAt, closeFile, cancelled, nullptr, 2u * 1024u * 1024u, nullptr};
}
