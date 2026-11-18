/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：布局缓存块的增量写入、最后提交、延迟读取和故障隔离回归。
 * English: Incremental layout-cache writes, root-last commits, lazy reads and failure isolation.
 */
#include "platform.h"
#include "../layout_cache.h"
#include "cre_mcu_internal.h"
#include <cstring>
#include <map>
#include <vector>

#include "cache_records.h"
static void exact(LVStreamRef stream, const std::vector<uint8_t> &expected) {
    assert(!stream.isNull() && stream->GetSize() == expected.size()); assert(stream->SetPos(0) == 0);
    std::vector<uint8_t> actual(expected.size()); lvsize_t count = 0;
    assert(stream->Read(actual.data(), actual.size(), &count) == LVERR_OK && count == actual.size() && actual == expected);
}
int main() {
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    Records records; CreLayoutRecords io{&records, read, write, remove};
    std::vector<uint8_t> content(16384 * 4 + 123);
    for (size_t i = 0; i < content.size(); ++i) content[i] = uint8_t((i * 79 + i / 977) % 251);
    assert(cre_layout_stream(io, 1, true).isNull());
    auto stream = cre_layout_stream(io, 1, false); assert(!stream.isNull()); lvsize_t count = 0;
    assert(stream->Write(content.data(), content.size(), &count) == LVERR_OK && count == content.size());
    assert(stream->Flush(true) == LVERR_OK && cre_layout_stream(io, 1, true).isNull());
    assert(cre_layout_commit(stream)); exact(stream, content); stream.Clear();
    auto restored = cre_layout_stream(io, 1, true); exact(restored, content); restored.Clear();
    restored = cre_layout_stream(io, 1, true); unsigned reads = records.reads;
    assert(restored->SetPos(content.size() - 100) == content.size() - 100);
    uint8_t tail[100]; assert(restored->Read(tail, sizeof(tail), &count) == LVERR_OK && count == sizeof(tail));
    assert(!std::memcmp(tail, content.data() + content.size() - 100, sizeof(tail)) && records.reads == reads + 1);
    auto writes = records.writes; assert(cre_layout_commit(restored) && records.writes == writes); restored.Clear();
    auto baseline = records.data;
    for (unsigned failure = 0; failure < 3; ++failure) {
        records.data = baseline; records.writes = 0; records.fail_at = failure;
        stream = cre_layout_stream(io, 1, true); assert(!stream.isNull());
        uint8_t changed = 99; assert(stream->SetPos(10) == 10);
        assert(stream->Write(&changed, 1, &count) == LVERR_OK && count == 1);
        bool committed = cre_layout_commit(stream); assert(committed == (failure >= 2)); stream.Clear();
        auto expected = content; if (committed) expected[10] = changed;
        restored = cre_layout_stream(io, 1, true); exact(restored, expected); restored.Clear();
    }
    records.data = baseline; records.fail_at = UINT32_MAX; records.writes = 0;
    stream = cre_layout_stream(io, 1, true); assert(!stream.isNull());
    uint8_t changed = 88; stream->SetPos(10); assert(stream->Write(&changed, 1, &count) == LVERR_OK);
    platform.cancel = true; bool cancelled = false;
    try { (void)cre_layout_commit(stream); } catch (const CreMcuFailure &error) { cancelled = error.code == CRE_CANCELLED; }
    assert(cancelled); platform.cancel = false;
    restored = cre_layout_stream(io, 1, true); exact(restored, content); restored.Clear();
    assert(cre_layout_commit(stream)); content[10] = changed; exact(stream, content); stream.Clear();
    assert(records.removed && records.data.size() == baseline.size());
    records.damage = true; assert(cre_layout_stream(io, 1, true).isNull()); records.damage = false;
    restored = cre_layout_stream(io, 1, true); assert(!restored.isNull()); records.damage = true;
    assert(restored->Read(tail, sizeof(tail), &count) == LVERR_FAIL && !count); restored.Clear(); records.damage = false;
    assert(cre_layout_stream(io, 2, true).isNull());
    for (size_t failure = 0; failure < 12; ++failure) {
        auto live = platform.live; platform.fail_at = platform.allocations + failure;
        try { restored = cre_layout_stream(io, 1, true); if (!restored.isNull()) (void)cre_layout_commit(restored); }
        catch (const std::bad_alloc &) {}
        restored.Clear(); platform.fail_at = SIZE_MAX; assert(platform.live == live);
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    std::puts("Layout blocks: lazy target read, incremental commit, unchanged skip, old-root protection, reclamation and faults passed");
}
