/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：布局缓存测试的原子记录存储与故障注入。
 * English: Atomic record storage and fault injection for layout-cache tests.
 */
#pragma once
#include <cstring>
#include <map>
#include <vector>
struct Records {
    std::map<uint64_t, std::vector<uint8_t>> data;
    unsigned reads = 0, writes = 0, removed = 0, fail_at = UINT32_MAX, fail_read_at = UINT32_MAX;
    bool fail_read = false, damage = false, fail_after_write = false;
};
static bool read(void *context, uint64_t key, void *out, size_t capacity, size_t *actual) {
    auto &r = *static_cast<Records *>(context); bool failed = r.reads++ == r.fail_read_at; *actual = 0;
    auto found = r.data.find(key);
    if (failed || r.fail_read || found == r.data.end() || found->second.size() > capacity) return false;
    *actual = found->second.size(); std::memcpy(out, found->second.data(), *actual);
    if (r.damage && *actual) static_cast<uint8_t *>(out)[*actual / 2] ^= 1;
    return true;
}
static bool write(void *context, uint64_t key, const void *data, size_t bytes) {
    auto &r = *static_cast<Records *>(context);
    bool failed = r.writes++ == r.fail_at;
    if (failed && !r.fail_after_write) return false;
    auto *p = static_cast<const uint8_t *>(data); r.data[key] = {p, p + bytes}; return !failed;
}
static void remove(void *context, uint64_t key) { auto &r = *static_cast<Records *>(context); ++r.removed; r.data.erase(key); }
