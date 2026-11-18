/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：分页暂停状态重建后必须保持页界、脚注、流和已发布记录不变。
 * English: Restored pagination retains page boundaries, footnotes, flows and published records.
 */
#include "platform.h"
#include "lvpagesplitter.h"
#include "cre_mcu_internal.h"
#include <cstring>
#include <memory>
#include <vector>

using Bytes = std::vector<lUInt8>;
static Bytes bytes(SerialBuf &buffer) { return {buffer.buf(), buffer.buf() + buffer.pos()}; }
static Bytes page(LVRendPageInfo *p) { SerialBuf out(0); assert(p->serialize(out)); return bytes(out); }
static std::unique_ptr<LVRendPageContext> source(LVRendPageList &pages, int height) {
    auto context = std::make_unique<LVRendPageContext>(&pages, height, 20);
    for (int i = 0; i < 240; ++i) {
        if (i == 80 || i == 150) context->newFlow(i == 80);
        int flags = i % 11 < 3 ? RN_SPLIT_AFTER_AVOID : i % 13 == 0 ? RN_SPLIT_AFTER_ALWAYS : 0;
        if (i % 7 < 4) flags |= RN_LINE_IS_RTL;
        context->AddLine(i * 24, i * 24 + (i % 29 == 0 ? height * 2 : 20), flags);
        if (i == 5 || i == 6 || i == 45 || i == 95) context->addLink(U"note-a");
        if (i == 6 || i == 20) context->addLink(U"note-alias");
        if (i == 120 || i == 180) context->addLink(U"note-b");
    }
    lString32Collection aliases; aliases.add(U"note-a"); aliases.add(U"note-alias");
    context->enterFootNote(aliases, false);
    for (int i = 0; i < 36; ++i) context->AddLine(6000 + i * 20, 6016 + i * 20, i % 2 ? RN_LINE_IS_RTL : 0);
    context->leaveFootNote(); context->enterFootNote(U"note-b");
    for (int i = 0; i < 12; ++i) context->AddLine(7000 + i * 20, 7016 + i * 20, 0);
    context->leaveFootNote();
    return context;
}
static std::unique_ptr<LVRendPageContext> restore(Bytes &g, Bytes &c, LVRendPageList &pages) {
    SerialBuf geometry(g.data(), g.size()), cursor(c.data(), c.size());
    return std::unique_ptr<LVRendPageContext>(LVRendPageContext::restoreMcuState(geometry, cursor, &pages));
}
int main() {
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    unsigned resumes = 0, notes = 0;
    for (int height : {120, 240, 480}) {
        LVRendPageList expected; auto complete = source(expected, height); assert(complete->FinalizeTo(0));
        for (int i = 0; i < expected.length(); ++i) notes += expected[i]->footnotes.length();
        LVRendPageList actual; auto context = source(actual, height);
        Bytes initialGeometry;
        while (!context->FinalizeTo(actual.length() + 2)) {
            assert(actual.length() < expected.length());
            for (int i = 0; i < actual.length(); ++i) assert(page(actual[i]) == page(expected[i]));
            SerialBuf geometry(0), cursor(0);
            assert(context->saveMcuState(geometry, cursor)); auto g = bytes(geometry), c = bytes(cursor);
            if (initialGeometry.empty()) initialGeometry = g; else assert(initialGeometry == g);
            if (!resumes) {
                for (size_t i = 0; i < g.size(); i += 37) {
                    g[i] ^= 1; assert(!restore(g, c, actual)); g[i] ^= 1;
                }
                for (size_t i = 0; i < c.size(); ++i) {
                    c[i] ^= 1; assert(!restore(g, c, actual)); c[i] ^= 1;
                }
                for (size_t n = 0; n < g.size(); n += 37) {
                    Bytes shortRecord(g.begin(), g.begin() + n); assert(!restore(shortRecord, c, actual));
                }
                platform.cancel = true; bool cancelled = false;
                try { (void)restore(g, c, actual); } catch (const CreMcuFailure &error) { cancelled = error.code == CRE_CANCELLED; }
                assert(cancelled); platform.cancel = false;
            }
            context.reset(); context = restore(g, c, actual); assert(context); ++resumes;
        }
        assert(actual.length() == expected.length());
        for (int i = 0; i < actual.length(); ++i) assert(page(actual[i]) == page(expected[i]));
    }
    assert(resumes > 30 && notes > 10);
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    std::printf("%u pagination restores match continuous pages with %u footnote fragments; corruption/truncation/cancellation passed\n", resumes, notes);
}
