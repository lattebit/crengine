/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：有界布局缓存流，使用内容键和最后提交的根隔离未完成写入。
 * English: Bounded layout-cache stream with content keys and a root committed last to isolate incomplete writes.
 */
#include "layout_cache.h"
#include "cre_mcu_internal.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {
constexpr size_t blockSize = 16 * 1024, blockLimit = 4096, rootLimit = 32 + blockLimit * 8;
constexpr uint64_t rootMagic = UINT64_C(0x31544f4f524c434d), seed = UINT64_C(14695981039346656037);
uint64_t digest(uint64_t value, const void *data, size_t bytes) {
    auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; ++i) { value ^= p[i]; value *= UINT64_C(1099511628211); }
    return value ? value : 1;
}
void put(uint8_t *out, uint64_t value) { for (unsigned i = 0; i < 8; ++i) out[i] = uint8_t(value >> (i * 8)); }
uint64_t get(const uint8_t *in) { uint64_t value = 0; for (unsigned i = 0; i < 8; ++i) value |= uint64_t(in[i]) << (i * 8); return value; }
template<class T> struct Allocator {
    using value_type = T;
    Allocator() = default;
    template<class U> Allocator(const Allocator<U> &) {}
    T *allocate(size_t count) {
        if (count > SIZE_MAX / sizeof(T)) throw std::bad_alloc();
        auto *data = static_cast<T *>(cre_mcu_allocate_optional(count * sizeof(T)));
        if (!data) throw std::bad_alloc();
        return data;
    }
    void deallocate(T *data, size_t) { cre_mcu_release(data); }
    template<class U> bool operator==(const Allocator<U> &) const { return true; }
    template<class U> bool operator!=(const Allocator<U> &) const { return false; }
};
using Keys = std::vector<uint64_t, Allocator<uint64_t>>;
using Buffer = std::unique_ptr<uint8_t, decltype(&cre_mcu_release)>;
Buffer buffer(size_t count) { return Buffer(static_cast<uint8_t *>(cre_mcu_allocate_optional(count)), cre_mcu_release); }
template<class Action> lverror_t contained(Action action) {
    // 中文：上游缓存函数显式释放临时 C 缓冲，不能用异常跳过这些清理路径。
    // English: Upstream cache functions explicitly free temporary C buffers; exceptions must not bypass their cleanup.
    try { auto result = action(); if (result != LVERR_OK) cre_mcu_record_error(CRE_IO); return result; }
    catch (const CreMcuFailure &error) { cre_mcu_record_error(error.code); }
    catch (const std::bad_alloc &) { cre_mcu_record_error(CRE_MEMORY); }
    catch (...) { cre_mcu_record_error(CRE_IO); }
    return LVERR_FAIL;
}
class LayoutStream : public LVNamedStream {
    CreLayoutRecords records;
    uint64_t layout, rootKey, rootDigest = 0;
    lvsize_t length = 0, position = 0;
    Keys blocks, committed, written;
    Buffer data = buffer(blockSize);
    size_t loaded = SIZE_MAX;
    bool dirty = false, failed = false;
    uint64_t blockKey() const { return digest(layout ^ seed, data.get(), blockSize); }
    bool flushBlock() {
        if (!dirty) return true;
        cre_mcu_checkpoint(); uint64_t key = blockKey();
        if (blocks.size() <= loaded) blocks.resize(loaded + 1);
        if (blocks[loaded] != key) {
            // 中文：先保留回收账目，分配失败时不能留下未记录的新块。
            // English: Reserve reclamation bookkeeping before writing a new block.
            written.push_back(key);
            if (!records.write(records.context, key, data.get(), blockSize)) { written.pop_back(); return false; }
            blocks[loaded] = key;
        }
        dirty = false; return true;
    }
    bool select(size_t index) {
        if (index == loaded) return true;
        if (!flushBlock()) return false;
        loaded = SIZE_MAX;
        if (index < blocks.size() && blocks[index]) {
            size_t actual = 0;
            if (!records.read(records.context, blocks[index], data.get(), blockSize, &actual)
                || actual != blockSize || blockKey() != blocks[index]) return false;
        } else std::memset(data.get(), 0, blockSize);
        loaded = index; return true;
    }
public:
    LayoutStream(CreLayoutRecords callbacks, uint64_t identity) : records(callbacks), layout(identity),
        rootKey(digest(seed, &identity, sizeof(identity)) ^ rootMagic) { m_mode = LVOM_APPEND; }
    bool open(bool restore) {
        if (!data || !records.read || !records.write) return false;
        if (!restore) return true;
        auto root = buffer(rootLimit); if (!root) return false;
        size_t bytes = 0;
        if (!records.read(records.context, rootKey, root.get(), rootLimit, &bytes) || bytes < 32 || bytes > rootLimit) return false;
        uint64_t size = get(root.get() + 8), count = get(root.get() + 16), checksum = get(root.get() + bytes - 8);
        if (get(root.get()) != rootMagic || count > blockLimit || bytes != 32 + count * 8
            || size > blockSize * blockLimit || count != (size + blockSize - 1) / blockSize
            || checksum != digest(seed, root.get(), bytes - 8)) return false;
        blocks.resize(size_t(count));
        for (size_t i = 0; i < blocks.size(); ++i) blocks[i] = get(root.get() + 24 + i * 8);
        committed = blocks; length = lvsize_t(size); rootDigest = checksum; return true;
    }
    bool commit() {
        // 中文：上游部分刷新路径不传播错误；一次失败后禁止发布该工作副本。
        // English: Some upstream flush paths ignore failures; never publish a working copy after a failed operation.
        if (failed) return false;
        if (!flushBlock()) return false;
        size_t count = (length + blockSize - 1) / blockSize, bytes = 32 + count * 8;
        auto root = buffer(bytes); if (!root) return false;
        blocks.resize(count); put(root.get(), rootMagic); put(root.get() + 8, length); put(root.get() + 16, count);
        for (size_t i = 0; i < count; ++i) put(root.get() + 24 + i * 8, blocks[i]);
        uint64_t checksum = digest(seed, root.get(), bytes - 8); put(root.get() + bytes - 8, checksum);
        Keys retained = blocks;
        if (checksum != rootDigest) {
            cre_mcu_checkpoint();
            if (!records.write(records.context, rootKey, root.get(), bytes)) return false;
            rootDigest = checksum;
        }
        // 中文：成功发布后才回收旧块；取消时宁可留下可回收记录。
        // English: Reclaim only after publication; cancellation may leave reclaimable records behind.
        if (records.remove) {
            Keys live = blocks; std::sort(live.begin(), live.end());
            for (const auto *list : {&committed, &written}) for (auto key : *list) {
                cre_mcu_checkpoint();
                if (key && !std::binary_search(live.begin(), live.end(), key)) records.remove(records.context, key);
            }
        }
        committed.swap(retained); written.clear(); return true;
    }
    lverror_t GetSize(lvsize_t *out) override { if (out) *out = length; return LVERR_OK; }
    lvsize_t GetSize() override { return length; }
    lvpos_t GetPos() override { return position; }
    bool Eof() override { return position >= length; }
    lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t *out) override {
        int64_t base = origin == LVSEEK_SET ? 0 : origin == LVSEEK_CUR ? position : origin == LVSEEK_END ? length : -1;
        if (base < 0 || offset < -base || offset > int64_t(blockSize * blockLimit) - base) return LVERR_FAIL;
        position = lvpos_t(base + offset); if (out) *out = position; return LVERR_OK;
    }
    lverror_t SetSize(lvsize_t size) override {
        // 中文：布局文件仅增长，避免缩小后残留旧尾部字节再次可见。
        // English: Layout files grow only, preventing old tail bytes from becoming visible after truncation.
        if (size < length || size > blockSize * blockLimit) return LVERR_FAIL;
        length = size; return LVERR_OK;
    }
    lverror_t readBytes(void *out, lvsize_t bytes, lvsize_t *actual) {
        if (actual) *actual = 0;
        bytes = position < length ? std::min(bytes, length - position) : 0;
        lvsize_t done = 0;
        while (done < bytes) {
            cre_mcu_checkpoint(); size_t index = position / blockSize, offset = position % blockSize;
            if (!select(index)) return LVERR_FAIL;
            size_t count = std::min<size_t>(bytes - done, blockSize - offset);
            std::memcpy(static_cast<uint8_t *>(out) + done, data.get() + offset, count);
            position += count; done += count; if (actual) *actual = done;
        }
        return LVERR_OK;
    }
    lverror_t writeBytes(const void *source, lvsize_t bytes, lvsize_t *actual) {
        if (actual) *actual = 0;
        if (bytes > blockSize * blockLimit - position) return LVERR_FAIL;
        lvsize_t done = 0;
        while (done < bytes) {
            cre_mcu_checkpoint(); size_t index = position / blockSize, offset = position % blockSize;
            if (!select(index)) return LVERR_FAIL;
            size_t count = std::min<size_t>(bytes - done, blockSize - offset);
            std::memcpy(data.get() + offset, static_cast<const uint8_t *>(source) + done, count); dirty = true;
            position += count; done += count; length = std::max(length, position); if (actual) *actual = done;
        }
        return LVERR_OK;
    }
    lverror_t Read(void *out, lvsize_t bytes, lvsize_t *actual) override {
        auto result = contained([&] { return readBytes(out, bytes, actual); }); failed |= result != LVERR_OK; return result;
    }
    lverror_t Write(const void *source, lvsize_t bytes, lvsize_t *actual) override {
        auto result = contained([&] { return writeBytes(source, bytes, actual); }); failed |= result != LVERR_OK; return result;
    }
    lverror_t Flush(bool) override {
        auto result = contained([&] { return flushBlock() ? LVERR_OK : LVERR_FAIL; }); failed |= result != LVERR_OK; return result;
    }
};
}
LVStreamRef cre_layout_stream(CreLayoutRecords records, uint64_t layout, bool restore) {
    LVStreamRef stream(new LayoutStream(records, layout));
    return static_cast<LayoutStream *>(stream.get())->open(restore) ? stream : LVStreamRef();
}
bool cre_layout_commit(LVStreamRef stream) {
    return !stream.isNull() && static_cast<LayoutStream *>(stream.get())->commit();
}
