/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：解压后的章节和样式源最多占 512 KiB；读者共享数据，检查点才写 TF。
 * English: Decompressed chapters/styles use at most 512 KiB; readers share bytes and only checkpoints write TF.
 */
#include "source_cache.h"
#include "cre_mcu_internal.h"
#include <algorithm>
#include <cstring>
#include <array>

namespace {
constexpr size_t source_limit = 256 * 1024, cache_limit = 512 * 1024;
struct Data {
    uint8_t *bytes = nullptr;
    size_t size = 0;
    uint64_t key = 0, used = 0;
    bool dirty = false;
    ~Data() { cre_mcu_release(bytes); }
};
class SourceStream final : public LVNamedStream {
    std::shared_ptr<Data> data;
    lvpos_t position = 0;
public:
    SourceStream(std::shared_ptr<Data> value, const lChar32 *name) : data(std::move(value)) { m_mode = LVOM_READ; SetName(name); }
    lvsize_t GetSize() override { return data->size; }
    lverror_t GetSize(lvsize_t *out) override { if (out) *out = data->size; return LVERR_OK; }
    lvpos_t GetPos() override { return position; }
    lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t *out) override {
        int64_t base = origin == LVSEEK_SET ? 0 : origin == LVSEEK_CUR ? position : origin == LVSEEK_END ? data->size : -1;
        int64_t next = base + offset;
        if (base < 0 || next < 0 || uint64_t(next) > data->size) return LVERR_FAIL;
        position = next; if (out) *out = position; return LVERR_OK;
    }
    lverror_t SetSize(lvsize_t) override { return LVERR_FAIL; }
    lverror_t Write(const void *, lvsize_t, lvsize_t *actual) override { if (actual) *actual = 0; return LVERR_FAIL; }
    lverror_t Read(void *out, lvsize_t count, lvsize_t *actual) override {
        cre_mcu_checkpoint(); count = std::min<lvsize_t>(count, data->size - position);
        if (count) std::memcpy(out, data->bytes + position, count);
        position += count; if (actual) *actual = count; return LVERR_OK;
    }
    bool Eof() override { return position == data->size; }
};
uint64_t keyFor(const lString32 &name) {
    auto text = UnicodeToUtf8(name); uint64_t key = UINT64_C(14695981039346656037);
    for (int i = 0; i < text.length(); ++i) key = (key ^ uint8_t(text[i])) * UINT64_C(1099511628211);
    return key;
}
bool eligible(lString32 name) {
    name.lowercase();
    return name.endsWith(".xhtml") || name.endsWith(".html") || name.endsWith(".htm") || name.endsWith(".css");
}
}
class CreSourceCache {
public:
    LVContainerRef archive;
    cre_source_cache callbacks{};
    std::array<std::shared_ptr<Data>, 8> entries{};
    uint64_t clock = 0;
    explicit CreSourceCache(LVContainerRef input) : archive(input) {}
    LVStreamRef open(const lChar32 *name, lvopen_mode_t mode) {
        if (mode != LVOM_READ || !eligible(lString32(name))) return archive->OpenStream(name, mode);
        uint64_t key = keyFor(lString32(name));
        for (auto &entry : entries) if (entry && entry->key == key) {
            entry->used = ++clock; return LVStreamRef(new SourceStream(entry, name));
        }
        const auto *info = archive->GetObjectInfo(lString32(name));
        size_t bytes = info ? info->GetSize() : 0;
        if (!bytes || bytes > source_limit) return archive->OpenStream(name, mode);
        size_t slot = entries.size();
        for (;;) {
            size_t used = 0, victim = entries.size(); slot = entries.size();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (!entries[i]) { slot = i; continue; }
                used += entries[i]->size;
                if (entries[i].use_count() == 1 && (victim == entries.size() || entries[i]->used < entries[victim]->used)) victim = i;
            }
            if (slot < entries.size() && bytes <= cache_limit - used) break;
            if (victim == entries.size()) return archive->OpenStream(name, mode);
            entries[victim].reset();
        }
        auto data = std::make_shared<Data>();
        data->bytes = static_cast<uint8_t *>(cre_mcu_allocate_optional(bytes));
        if (!data->bytes) return archive->OpenStream(name, mode);
        data->size = bytes; data->key = key; data->used = ++clock;
        bool hit = callbacks.read && callbacks.read(callbacks.context, key, data->bytes, bytes);
        cre_mcu_checkpoint();
        if (!hit) {
            auto input = archive->OpenStream(name, mode); if (input.isNull()) return input;
            if (input->GetSize() != bytes) return input;
            for (size_t at = 0; at < bytes;) {
                cre_mcu_checkpoint(); lvsize_t actual = 0, count = std::min<size_t>(4096, bytes - at);
                if (input->Read(data->bytes + at, count, &actual) != LVERR_OK || actual != count) throw CreMcuFailure{CRE_IO};
                at += count;
            }
            data->dirty = true;
        }
        auto stream = LVStreamRef(new SourceStream(data, name));
        entries[slot] = std::move(data); return stream;
    }
};
namespace {
class Container final : public LVContainer {
    std::shared_ptr<CreSourceCache> cache;
public:
    explicit Container(std::shared_ptr<CreSourceCache> value) : cache(std::move(value)) {}
    LVContainer *GetParentContainer() override { return cache->archive->GetParentContainer(); }
    const LVContainerItemInfo *GetObjectInfo(int index) override { return cache->archive->GetObjectInfo(index); }
    const LVContainerItemInfo *GetObjectInfo(lString32 name) override { return cache->archive->GetObjectInfo(name); }
    int GetObjectCount() const override { return cache->archive->GetObjectCount(); }
    lverror_t GetSize(lvsize_t *out) override { return cache->archive->GetSize(out); }
    const lChar32 *GetName() override { return cache->archive->GetName(); }
    void SetName(const lChar32 *name) override { cache->archive->SetName(name); }
    LVStreamRef OpenStream(const lChar32 *name, lvopen_mode_t mode) override { return cache->open(name, mode); }
};
}
std::shared_ptr<CreSourceCache> cre_source_cache_create(LVContainerRef archive, LVContainerRef &wrapped) {
    auto cache = std::make_shared<CreSourceCache>(archive);
    wrapped = LVContainerRef(new Container(cache)); return cache;
}
void cre_source_cache_bind(CreSourceCache &cache, const cre_source_cache &callbacks) { cache.callbacks = callbacks; }
void cre_source_cache_checkpoint(CreSourceCache &cache) {
    for (auto &entry : cache.entries) if (entry && entry->dirty && cache.callbacks.write) {
        cre_mcu_checkpoint();
        if (cache.callbacks.write(cache.callbacks.context, entry->key, entry->bytes, entry->size)) entry->dirty = false;
    }
}
