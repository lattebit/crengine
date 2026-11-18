/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：紧凑页目录只定位，正文页与脚注按需读取，调用间回收解码对象。
 * English: Compact page metadata locates pages while page/footnote records decode on demand and are reclaimed between calls.
 */
#include "platform.h"
#include "cre_mcu_internal.h"
#include "lvpagesplitter.h"
#include <vector>
#include <cstring>
struct Pages { std::vector<std::vector<uint8_t>> records; unsigned reads = 0, fail = UINT32_MAX; };
static bool readPage(void *context, unsigned index, SerialBuf &out) {
    auto &p = *static_cast<Pages *>(context); ++p.reads;
    if (index == p.fail || index >= p.records.size()) return false;
    auto &record = p.records[index]; auto *bytes = static_cast<uint8_t *>(std::malloc(record.size())); assert(bytes);
    std::memcpy(bytes, record.data(), record.size()); out.set(bytes, int(record.size())); out.setPos(0); return true;
}
int main() {
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    {
        Pages records; LVRendPageList fresh;
        for (int i = 0; i < 5000; ++i) {
            auto *page = new LVRendPageInfo(i * 620, 600, i); page->flow = i / 2000;
            for (int note = 0; note < i % 4; ++note) page->footnotes.add(LVPageFootNoteInfo(i * 620 + note * 10, 10));
            fresh.add(page); SerialBuf encoded(0); assert(page->serialize(encoded));
            records.records.emplace_back(encoded.buf(), encoded.buf() + encoded.pos());
        }
        fresh.setHasNonLinearFlows(true); SerialBuf index(0); assert(fresh.serializeMcuIndex(index));
        assert(index.pos() < fresh.length() * 10);
        LVRendPageList restored; SerialBuf input(index.buf(), index.pos());
        assert(restored.restoreMcuIndex(input, &records, readPage));
        assert(restored.length() == fresh.length() && !restored.mcuLoadedPages() && !records.reads && restored.hasNonLinearFlows());
        auto last = restored[4999]; assert(last->start == fresh[4999]->start && last->footnotes.length() == 3 && records.reads == 1);
        for (int i : {0, 1, 2500, 4999}) for (int direction : {-1, 0, 1})
            assert(restored.FindNearestPage(i * 620 + 100, direction) == fresh.FindNearestPage(i * 620 + 100, direction));
        assert(records.reads == 1);
        assert(restored[10]->footnotes.length() == 2 && restored.mcuLoadedPages() == 2);
        restored.trimMcuPages(4999); assert(restored.mcuLoadedPages() == 1);
        SerialBuf again(0); assert(restored.serializeMcuIndex(again) && records.reads == 2);
        assert(index.pos() == again.pos() && !std::memcmp(index.buf(), again.buf(), index.pos()));
        records.fail = 42; bool failed = false;
        try { (void)restored[42]; } catch (const CreMcuFailure &error) { failed = error.code == CRE_CONTENT; }
        assert(failed && restored.mcuLoadedPages() == 1); records.fail = UINT32_MAX;
        records.records[42][0] ^= 128; failed = false;
        try { (void)restored[42]; } catch (const CreMcuFailure &error) { failed = error.code == CRE_CONTENT; }
        assert(failed && restored.mcuLoadedPages() == 1); records.records[42][0] ^= 128;
        unsigned before = records.reads; platform.cancel = true; failed = false;
        try { (void)restored[42]; } catch (const CreMcuFailure &error) { failed = error.code == CRE_CANCELLED; }
        platform.cancel = false; assert(failed && records.reads == before);
        for (size_t offset : {1u, 5u}) {
            size_t live = platform.live;
            platform.cancel_at = platform.checkpoints + offset; failed = false;
            try { restored.captureMcuIndex(&records, readPage); }
            catch (const CreMcuFailure &error) { failed = error.code == CRE_CANCELLED; }
            platform.cancel_at = SIZE_MAX;
            assert(failed && platform.live == live && restored.mcuCachedPages() == 5000);
            assert(restored[4999] == last);
        }
        for (size_t offset : {5001u, 5005u}) {
            size_t live = platform.live;
            {
                LVRendPageList interrupted; SerialBuf retry(index.buf(), index.pos());
                platform.cancel_at = platform.checkpoints + offset; failed = false;
                try { interrupted.restoreMcuIndex(retry, &records, readPage); }
                catch (const CreMcuFailure &error) { failed = error.code == CRE_CANCELLED; }
                platform.cancel_at = SIZE_MAX; assert(failed);
            }
            assert(platform.live == live);
        }
        SerialBuf truncated(index.buf(), index.pos() - 1); assert(!LVRendPageList::validateMcuIndex(truncated));
        index.buf()[index.pos() / 2] ^= 1; SerialBuf corrupt(index.buf(), index.pos()); assert(!LVRendPageList::validateMcuIndex(corrupt));
        index.buf()[index.pos() / 2] ^= 1;
        platform.fail_at = platform.allocations; SerialBuf unavailable(index.buf(), index.pos());
        assert(!restored.restoreMcuIndex(unavailable, &records, readPage)); platform.fail_at = SIZE_MAX;
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    std::puts("5000-page metadata: last page needs one record, positioning reads none, bounded live pages, corrupt/read/cancel/allocation failures passed");
}
