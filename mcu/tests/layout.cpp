/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：六种格式真实排版，以及 Markdown 直接 DOM 与完整 HTML 基准的逐页对照。
 * English: Real six-format layout and page-by-page Markdown direct-DOM versus complete-HTML comparison.
 */
#include "platform.h"
#include "lvdocview.h"
#include "lvfntman.h"
#include "epubfmt.h"
#include "cre_stylesheets.h"
#include <md4c-html.h>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
static std::unique_ptr<LVDocView> load(const std::string &path, int pixels, EpubIndex *index = nullptr, int chapter = -1, bool metadata = false, int initial_pages = 0) {
    auto view = std::make_unique<LVDocView>();
    view->setBackgroundColor(0xffffff); view->setTextColor(0);
    view->setDefaultFontFace(lString8("mcu-test"));
    view->setEpubIndex(index, chapter);
    view->setStatusFontFace(lString8("mcu-test"));
    view->setFontSize(pixels); view->setPageHeaderInfo(0); view->setShowCover(false);
    view->setPageMargins(lvRect(12, 12, 12, 12)); view->setViewMode(DVM_PAGES, 1);
    view->Resize(480, 640);
    view->setStyleSheet(lString8(cre_styles::html) + "body { margin: 0; } p { margin: 0 0 0.5em 0; } h1 { font-size: 1.5em; page-break-before: auto; } h2 { font-size: 1.2em; } pre { white-space: pre; } table { border-collapse: collapse; } td, th { padding: 4px; border: 1px solid black; }");
    if (!view->LoadDocument(path.c_str(), metadata)) { std::fprintf(stderr, "load failed: %s\n", path.c_str()); std::abort(); }
    view->getDocument()->setMcuPageLimit(initial_pages);
    if (!metadata) { view->Render(); assert(view->getPageCount() > 0); }
    return view;
}
static std::vector<uint8_t> draw(LVDocView &view, int page) {
    assert(view.goToPage(page));
    LVGrayDrawBuf buffer(480, 640, 8); buffer.Clear(0xffffff); view.Draw(buffer, false);
    std::vector<uint8_t> pixels(480 * 640);
    for (int y = 0; y < 640; ++y) std::memcpy(pixels.data() + y * 480, buffer.GetScanLine(y), 480);
    return pixels;
}
int main(int argc, char **argv) {
    assert(argc == 3); Platform platform; auto cfg = config(platform);
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    assert(fontMan->RegisterExternalFont(-1, Utf8ToUnicode(argv[1]), lString8("mcu-test"), 400, 100, false));
    for (const char *extension : {"txt", "html", "fb2", "md", "rtf", "epub"}) {
        auto view = load(std::string(argv[2]) + "/fixture." + extension, 24);
        assert(view->getPageCount() > 1);
        auto text = UnicodeToUtf8(view->getDocument()->getRootNode()->getText());
        assert(text.pos("Anchor target") >= 0);
        auto image = draw(*view, 0); size_t ink = 0;
        for (auto value : image) ink += value < 128;
        assert(ink > 100 && ink < image.size() / 2);
        auto partial = load(std::string(argv[2]) + "/fixture." + extension, 24, nullptr, -1, false, 1);
        auto first = draw(*partial, 0);
        for (int page = 0; page < view->getPageCount(); ++page) {
            partial->getDocument()->continueMcuPages(page + 1);
            assert(draw(*partial, 0) == first && draw(*partial, page) == draw(*view, page));
        }
        assert(partial->getDocument()->continueMcuPages(0) && partial->getPageCount() == view->getPageCount());
        std::printf("%s pages=%d ink=%zu\n", extension, view->getPageCount(), ink);
    }
    {
        // 中文：浮动、负边距、列表、跨页表格及延后脚注必须与完整分页逐像素一致。
        // English: Floats, negative margins, lists, spanning tables and deferred notes must match complete pagination pixel for pixel.
        auto path = std::string(argv[2]) + "/pagination.html";
        auto full = load(path, 24), partial = load(path, 24, nullptr, -1, false, 1);
        assert(!partial->getDocument()->mcuPagesComplete());
        auto first = draw(*partial, 0);
        for (int page = 0; page < full->getPageCount(); ++page) {
            partial->getDocument()->continueMcuPages(page + 1);
            assert(draw(*partial, 0) == first);
            assert(draw(*partial, page) == draw(*full, page));
        }
        assert(partial->getDocument()->continueMcuPages(0) && partial->getPageCount() == full->getPageCount());
    }
    {
        std::string path = std::string(argv[2]) + "/fixture.epub";
        EpubIndex index;
        auto metadata = load(path, 24, &index, 0, true);
        assert(index.ready && index.chapters.length() == 2 && index.toc.length() == 2);
        assert(metadata->getDocument()->getRootNode()->getText().empty());
        assert(index.toc[1].chapter == 1 && index.toc[1].title == U"Second");
        metadata.reset();
        auto reference = load(path, 24);
        int offset = 0;
        for (int chapter = 0; chapter < 2; ++chapter) {
            auto partial = load(path, 24, &index, chapter);
            auto text = partial->getDocument()->getRootNode()->getText();
            assert((text.pos(U"Second chapter") >= 0) == (chapter == 1));
            for (int page = 0; page < partial->getPageCount(); ++page) {
                if (draw(*partial, page) != draw(*reference, offset + page)) {
                    for (int which = 0; which < 2; ++which) {
                        auto pixels = which ? draw(*reference, offset + page) : draw(*partial, page);
                        std::string output = std::string(argv[2]) + (which ? "/reference.pgm" : "/partial.pgm");
                        FILE *file = std::fopen(output.c_str(), "wb"); assert(file);
                        std::fprintf(file, "P5\n480 640\n255\n"); std::fwrite(pixels.data(), 1, pixels.size(), file); std::fclose(file);
                    }
                    std::fprintf(stderr, "EPUB chapter pixels differ: chapter=%d page=%d offset=%d\n", chapter, page, offset); return 1;
                }
            }
            offset += partial->getPageCount();
        }
        assert(offset == reference->getPageCount());
    }
    std::string mdPath = std::string(argv[2]) + "/fixture.md";
    std::ifstream input(mdPath); std::string markdown((std::istreambuf_iterator<char>(input)), {}), html;
    assert(md_html(markdown.data(), markdown.size(), [](const MD_CHAR *text, MD_SIZE bytes, void *ctx) {
        static_cast<std::string *>(ctx)->append(text, bytes);
    }, &html, MD_FLAG_COLLAPSEWHITESPACE | MD_DIALECT_GITHUB, MD_HTML_FLAG_XHTML) == 0);
    std::string htmlPath = std::string(argv[2]) + "/markdown-reference.html";
    { std::ofstream out(htmlPath); out << "<html><head><title>fixture</title></head><body>" << html << "</body></html>"; }
    for (int pixels : {20, 32}) {
        auto direct = load(mdPath, pixels), reference = load(htmlPath, pixels);
        if (direct->getPageCount() != reference->getPageCount()) {
            std::fprintf(stderr, "Markdown page counts differ: %d %d\n", direct->getPageCount(), reference->getPageCount()); return 1;
        }
        for (int i = 0; i < direct->getPageCount(); ++i) {
            if (draw(*direct, i) != draw(*reference, i)) {
                for (int which = 0; which < 2; ++which) {
                    auto &view = which ? *reference : *direct;
                    auto pixels = draw(view, i);
                    std::string output = std::string(argv[2]) + (which ? "/md-reference" : "/md-direct");
                    FILE *file = std::fopen((output + ".pgm").c_str(), "wb"); assert(file);
                    std::fprintf(file, "P5\n480 640\n255\n"); std::fwrite(pixels.data(), 1, pixels.size(), file); std::fclose(file);
                    auto stream = LVCreateMemoryStream(); view.getDocument()->saveToStream(stream, "utf-8");
                    stream->SetPos(0); file = std::fopen((output + ".xml").c_str(), "wb"); assert(file);
                    char bytes[4096]; lvsize_t count;
                    while (stream->Read(bytes, sizeof(bytes), &count) == LVERR_OK && count) std::fwrite(bytes, 1, count, file);
                    std::fclose(file);
                }
                std::fprintf(stderr, "Markdown pixels differ: size=%d page=%d\n", pixels, i); return 1;
            }
        }
    }
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
    std::puts("Six formats and direct Markdown parity passed");
}
