/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：真实 DOM 缓存恢复后继续分页，并逐页对照冷排版像素。
 * English: Resume real DOM-cache layouts and compare each page with cold-layout pixels.
 */
#include "platform.h"
#include "../layout_cache.h"
#include "cache_records.h"
#include "lvdocview.h"
#include "lvfntman.h"
#include "cre_stylesheets.h"
#include "cre_mcu_internal.h"
#include <memory>
#include <string>

static std::unique_ptr<LVDocView> load(const std::string &path, LVStreamRef cache = LVStreamRef(), unsigned pages = 0, bool requireCache = true) {
    auto view = std::make_unique<LVDocView>();
    view->setDefaultFontFace(lString8("persist-test")); view->setStatusFontFace(lString8("persist-test"));
    view->setFontSize(24); view->setPageHeaderInfo(0); view->setShowCover(false);
    view->setPageMargins(lvRect(12, 12, 12, 12)); view->setViewMode(DVM_PAGES, 1); view->Resize(480, 640);
    view->setStyleSheet(lString8(cre_styles::html)); view->setMcuCacheStream(cache);
    assert(view->LoadDocument(path.c_str())); view->setShowCover(false);
    if (!cache.isNull() && requireCache) assert(view->mcuCacheLoaded());
    view->getDocument()->setMcuPageLimit(pages); view->Render(); return view;
}
static std::vector<uint8_t> draw(LVDocView &view, unsigned page) {
    assert(view.goToPage(page)); LVGrayDrawBuf buffer(480, 640, 8); buffer.Clear(0xffffff); view.Draw(buffer, false);
    std::vector<uint8_t> result(480 * 640);
    for (int y = 0; y < 640; ++y) std::memcpy(result.data() + y * 480, buffer.GetScanLine(y), 480);
    auto *pages = view.getPageList();
    assert(pages->mcuLoadedPages() <= pages->length() - pages->mcuCachedPages() + 5);
    pages->trimMcuPages(int(page));
    return result;
}
static void failures(const std::string &path, Platform &platform) {
    Records records; CreLayoutRecords io{&records, read, write, remove};
    auto view = load(path, LVStreamRef(), 1); auto first = draw(*view, 0); int prefix = view->getPageCount();
    auto stream = cre_layout_stream(io, 1, false);
    assert(view->getDocument()->saveMcuCache(stream, view->getPageList()) && cre_layout_commit(stream));
    view.reset(); stream.Clear(); auto baseline = records.data;
    for (bool after : {false, true}) for (unsigned fault = 0; fault < 10; ++fault) {
        records.data = baseline; cre_mcu_reset_error();
        stream = cre_layout_stream(io, 1, true); view = load(path, stream, 1);
        view->getDocument()->continueMcuPages(4); int next = view->getPageCount();
        records.fail_at = records.writes + fault; records.fail_after_write = after;
        try { if (view->getDocument()->saveMcuCache(stream, view->getPageList())) (void)cre_layout_commit(stream); }
        catch (const CreMcuFailure &) {}
        records.fail_at = UINT32_MAX; records.fail_after_write = false; view.reset(); stream.Clear(); cre_mcu_reset_error();
        stream = cre_layout_stream(io, 1, true); view = load(path, stream, 1);
        assert((view->getPageCount() == prefix || view->getPageCount() == next) && draw(*view, 0) == first);
        view.reset(); stream.Clear();
    }
    for (unsigned fault : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u}) {
        records.data = baseline; cre_mcu_reset_error();
        stream = cre_layout_stream(io, 1, true); view = load(path, stream, 1);
        view->getDocument()->continueMcuPages(4); int next = view->getPageCount();
        platform.cancel_at = platform.checkpoints + fault;
        try { if (view->getDocument()->saveMcuCache(stream, view->getPageList())) (void)cre_layout_commit(stream); }
        catch (const CreMcuFailure &) {}
        platform.cancel_at = SIZE_MAX; view.reset(); stream.Clear(); cre_mcu_reset_error();
        stream = cre_layout_stream(io, 1, true); view = load(path, stream, 1);
        assert((view->getPageCount() == prefix || view->getPageCount() == next) && draw(*view, 0) == first);
        view.reset(); stream.Clear();
    }
    for (unsigned fault = 0; fault < 24; ++fault) {
        records.data = baseline; cre_mcu_reset_error(); stream = cre_layout_stream(io, 1, true);
        records.fail_read_at = records.reads + fault;
        try { view = load(path, stream, 1, false); assert(draw(*view, 0) == first); }
        catch (const CreMcuFailure &) { view.reset(); view = load(path, LVStreamRef(), 1); assert(draw(*view, 0) == first); }
        records.fail_read_at = UINT32_MAX; view.reset(); stream.Clear(); cre_mcu_reset_error();
    }
    std::puts("Persistent layouts retain valid old/new roots through 20 write faults, 13 cancellations and 24 read faults");
}
int main(int argc, char **argv) {
    assert(argc == 3); Platform platform; auto cfg = config(platform); cfg.memory_limit = 6 * 1024 * 1024;
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    assert(fontMan->RegisterExternalFont(-1, Utf8ToUnicode(argv[1]), lString8("persist-test"), 400, 100, false));
    failures(std::string(argv[2]) + "/fixture.html", platform);
    for (const char *file : {"fixture.html", "fixture.txt", "fixture.fb2", "fixture.md", "fixture.rtf", "fixture.epub", "pagination.html"}) {
        auto path = std::string(argv[2]) + '/' + file; Records records; CreLayoutRecords io{&records, read, write, remove};
        auto full = load(path), partial = load(path, LVStreamRef(), 1);
        assert(!partial->getDocument()->mcuPagesComplete());
        auto first = draw(*partial, 0); int prefix = partial->getPageCount();
        auto stream = cre_layout_stream(io, 1, false); assert(!stream.isNull());
        assert(partial->getDocument()->saveMcuCache(stream, partial->getPageList()) && cre_layout_commit(stream));
        partial.reset(); stream.Clear();
        stream = cre_layout_stream(io, 1, true); partial = load(path, stream, 1);
        assert(!partial->getDocument()->mcuPagesComplete() && partial->getPageCount() == prefix && draw(*partial, 0) == first);
        for (int page = 0; page < full->getPageCount(); ++page) {
            partial->getDocument()->continueMcuPages(page + 1);
            assert(draw(*partial, page) == draw(*full, page));
            if (page == prefix + 1) {
                assert(partial->getDocument()->saveMcuCache(stream, partial->getPageList()) && cre_layout_commit(stream));
                partial.reset(); stream.Clear(); stream = cre_layout_stream(io, 1, true); partial = load(path, stream, 1);
                assert(partial->getPageCount() == page + 1 && draw(*partial, page) == draw(*full, page));
            }
        }
        assert(partial->getDocument()->continueMcuPages(0));
        assert(partial->getDocument()->saveMcuCache(stream, partial->getPageList()) && cre_layout_commit(stream));
        partial.reset(); stream.Clear(); stream = cre_layout_stream(io, 1, true); partial = load(path, stream, 1);
        assert(partial->getDocument()->mcuPagesComplete() && partial->getPageCount() == full->getPageCount());
        assert(draw(*partial, full->getPageCount() - 1) == draw(*full, full->getPageCount() - 1));
        assert(partial->getPageList()->mcuLoadedPages() <= 3);
        std::printf("%s cached pages=%d blocks=%zu writes=%u\n", file, full->getPageCount(), records.data.size(), records.writes);
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
}
