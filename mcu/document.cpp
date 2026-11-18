/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：工作任务中的文档会话与灰阶输出；应用不可访问 DOM 或字体对象。
 * English: Worker-owned document sessions and grayscale output; applications cannot access DOM or font objects.
 */
#include "cre_mcu_document.h"
#include "cre_mcu_internal.h"
#include "lvdocview.h"
#include "epubfmt.h"
#include "lvfntman.h"
#include "cre_stylesheets.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct cre_document {
    std::string path;
    cre_layout layout{};
    cre_metadata metadata{};
    EpubIndex index;
    LVStreamRef stream;
    std::unique_ptr<LVDocView> view;
    uint32_t chapter = UINT32_MAX;
    bool font_registered = false;
    std::vector<cre_hit> hits;
    ~cre_document() {
        view.reset();
        if (font_registered) { fontMan->UnregisterDocumentFonts(-1); fontMan->gc(); }
    }
};
namespace {
cre_document *active = nullptr;
template<typename Function> int boundary(Function function) {
    cre_mcu_reset_error();
    try {
        cre_mcu_checkpoint(); int result = function();
        if (result == CRE_OK) cre_mcu_checkpoint();
        return cre_mcu_error() != CRE_OK ? cre_mcu_error() : result;
    }
    catch (const CreMcuFailure &failure) { return failure.code; }
    catch (const std::bad_alloc &) { return CRE_MEMORY; }
    catch (...) { return CRE_CONTENT; }
}
void copy(char *out, size_t capacity, const lString32 &text) {
    auto utf8 = UnicodeToUtf8(text); size_t count = std::min(capacity - 1, static_cast<size_t>(utf8.length()));
    while (count && count < static_cast<size_t>(utf8.length()) && (static_cast<unsigned char>(utf8[count]) & 0xc0) == 0x80) --count;
    std::memcpy(out, utf8.c_str(), count); out[count] = 0;
}
bool valid(const cre_layout *layout) {
    return layout && layout->width >= 64 && layout->width <= 2048 && !(layout->width & 1) && layout->height >= 64 && layout->height <= 2048
        && layout->pixels >= 8 && layout->pixels <= 128 && layout->weight >= 100 && layout->weight <= 900
        && layout->margin <= (layout->width - 32) / 2 && layout->margin <= (layout->height - 32) / 2
        && layout->line_percent >= 75 && layout->line_percent <= 250 && layout->paragraph_quarters <= 16 && layout->indent <= 8
        && layout->font[0] && std::memchr(layout->font, 0, sizeof(layout->font));
}
int format(doc_format_t format) {
    switch (format) {
        case doc_format_epub: return 1; case doc_format_txt: return 2; case doc_format_html: return 3;
        case doc_format_fb2: return 4; case doc_format_md: return 5; case doc_format_rtf: return 6;
        default: return 0;
    }
}
void metadata(cre_document &d, CRPropRef props) {
    copy(d.metadata.title, sizeof(d.metadata.title), props->getStringDef(DOC_PROP_TITLE));
    copy(d.metadata.author, sizeof(d.metadata.author), props->getStringDef(DOC_PROP_AUTHORS));
    copy(d.metadata.language, sizeof(d.metadata.language), props->getStringDef(DOC_PROP_LANGUAGE));
    d.metadata.has_cover = !props->getStringDef(DOC_PROP_COVER_FILE).empty();
}
int load(cre_document &d, uint32_t chapter) {
    if (d.view && d.chapter == chapter) return CRE_OK;
    d.view.reset(); d.chapter = UINT32_MAX;
    if (!d.font_registered) {
        if (!fontMan->RegisterExternalFont(-1, Utf8ToUnicode(d.layout.font), lString8("mcu-reading"), 400, 100, false))
            return cre_mcu_error() == CRE_OK ? CRE_CONTENT : cre_mcu_error();
        d.font_registered = true;
    }
    auto view = std::make_unique<LVDocView>(8, true);
    view->setBackgroundColor(0xffffff); view->setTextColor(0);
    view->setDefaultFontFace(lString8("mcu-reading")); view->setStatusFontFace(lString8("mcu-reading"));
    view->setSourcePath(Utf8ToUnicode(d.path.c_str()));
    view->setFontSize(d.layout.pixels); view->setPageHeaderInfo(0); view->setShowCover(false);
    view->setPageMargins(lvRect(d.layout.margin, d.layout.margin, d.layout.margin, d.layout.margin));
    view->setViewMode(DVM_PAGES, 1); view->Resize(d.layout.width, d.layout.height);
    auto props = LVCreatePropsContainer();
    props->setInt(PROP_INTERLINE_SPACE, d.layout.line_percent); props->setInt(PROP_FONT_BASE_WEIGHT, d.layout.weight);
    view->propsApply(props);
    char css[512];
    std::snprintf(css, sizeof(css), "body { margin: 0; } p { margin: 0 0 %.2fem 0; text-indent: %uem; }", d.layout.paragraph_quarters / 4.0, d.layout.indent);
    lString8 name(d.path.c_str()); name.lowercase();
    std::string style(name.endsWith(".fb2") || name.endsWith(".rtf") ? cre_styles::fictionbook : name.endsWith(".txt") ? cre_styles::text : cre_styles::html);
    style += css;
    if (d.layout.override_style) {
        std::snprintf(css, sizeof(css), "p { font-family: mcu-reading !important; font-size: 1em !important; font-weight: %u !important; line-height: %u%% !important; margin-bottom: %.2fem !important; text-indent: %uem !important; }",
                      d.layout.weight, d.layout.line_percent, d.layout.paragraph_quarters / 4.0, d.layout.indent);
        style += css;
    }
    view->setStyleSheet(lString8(style.c_str()));
    if (d.index.ready) view->setEpubIndex(&d.index, static_cast<int>(chapter));
    d.stream->SetPos(0);
    if (!view->LoadDocument(d.stream)) return CRE_CONTENT;
    // 中文：封面作为独立资源提供，不插入正文页序列。
    // English: Expose covers as separate resources without inserting them into body pagination.
    view->setShowCover(false);
    d.metadata.format = format(view->getDocFormat());
    if (!d.metadata.format) return CRE_CONTENT;
    metadata(d, view->getDocProps());
    d.view = std::move(view); d.chapter = chapter;
    return CRE_OK;
}
int render(cre_document &d, uint8_t *pixels, size_t capacity, bool cover) {
    const auto &l = d.layout;
    if (!pixels || capacity < static_cast<size_t>(l.width / 2) * l.height) return CRE_INVALID;
    size_t bytes = static_cast<size_t>(l.width) * l.height;
    std::unique_ptr<uint8_t, decltype(&cre_mcu_release)> scratch(static_cast<uint8_t *>(cre_mcu_allocate(bytes)), cre_mcu_release);
    if (!scratch) return CRE_MEMORY;
    LVGrayDrawBuf buffer(l.width, l.height, 8, scratch.get()); buffer.Clear(0xffffff);
    if (cover) {
        auto image = d.view->getCoverPageImage(); if (image.isNull()) return CRE_CONTENT;
        lvRect rectangle(0, 0, l.width, l.height); d.view->drawCoverTo(&buffer, rectangle);
    } else d.view->Draw(buffer, false);
    cre_mcu_checkpoint();
    for (uint32_t y = 0; y < l.height; ++y) {
        const uint8_t *row = buffer.GetScanLine(y);
        for (uint32_t x = 0; x < l.width; x += 2) pixels[static_cast<size_t>(y) * l.width / 2 + x / 2] = (row[x] & 0xf0) | (row[x + 1] >> 4);
    }
    return CRE_OK;
}
void addHit(cre_document &d, lvRect rectangle, const lString32 &href, bool image) {
    if (href.empty()) return;
    auto start = rectangle.topLeft(), end = rectangle.bottomRight();
    if (!d.view->docToWindowPoint(start, false, true) || !d.view->docToWindowPoint(end, true, true)) return;
    int left = std::max(0, start.x), top = std::max(0, start.y);
    int right = std::min(static_cast<int>(d.layout.width), end.x), bottom = std::min(static_cast<int>(d.layout.height), end.y);
    if (right <= left || bottom <= top) return;
    cre_hit hit{}; hit.x = left; hit.y = top; hit.width = right - left; hit.height = bottom - top; hit.image = image;
    auto target = UnicodeToUtf8(href);
    if (target.length() >= static_cast<int>(sizeof(hit.href))) throw CreMcuFailure{CRE_CONTENT};
    std::strcpy(hit.href, target.c_str()); d.hits.push_back(hit);
}
void collectHits(cre_document &d) {
    d.hits.clear(); ldomXRangeList links; d.view->getCurrentPageLinks(links);
    for (int i = 0; i < links.length(); ++i) {
        cre_mcu_checkpoint(); LVArray<lvRect> rectangles; links[i]->getSegmentRects(rectangles, true);
        auto href = links[i]->getHRef();
        for (int n = 0; n < rectangles.length(); ++n) addHit(d, rectangles[n], href, false);
    }
    auto range = d.view->getPageDocumentRange();
    if (!range.isNull()) {
        LVArray<ldomNode *> images; range->getRangeText(U'\n', 0, &images);
        for (int i = 0; i < images.length(); ++i) {
            lvRect rectangle; ldomXPointer pointer(images[i], 0);
            if (pointer.getRectEx(rectangle)) addHit(d, rectangle, images[i]->getAttributeValue("src"), true);
        }
    }
}
}
extern "C" int cre_document_open(const char *path, const cre_layout *layout, cre_document **out) {
    if (out) *out = nullptr;
    if (!out || !path || !*path || std::strlen(path) >= 768 || !valid(layout)) return CRE_INVALID;
    if (active) return CRE_BUSY;
    int result = boundary([&]() -> int {
        auto document = std::make_unique<cre_document>(); auto &d = *document;
        d.path = path; d.layout = *layout; d.stream = LVOpenFileStream(path, LVOM_READ);
        if (d.stream.isNull()) return CRE_IO;
        d.metadata.source_bytes = d.stream->GetSize(); d.metadata.chapters = 1;
        if (DetectEpubFormat(d.stream, &d.index.archive)) {
            auto metadata_document = std::make_unique<ldomDocument>();
            if (!ImportEpubDocument(d.stream, metadata_document.get(), nullptr, nullptr, true, nullptr, nullptr, nullptr, &d.index)) return CRE_CONTENT;
            d.metadata.format = 1; d.metadata.chapters = d.index.chapters.length();
            if (!d.metadata.chapters) return CRE_CONTENT;
            metadata(d, d.index.properties);
        } else {
            int result = load(d, 0); if (result != CRE_OK) return result;
        }
        active = document.release(); *out = active; return CRE_OK;
    });
    if (result != CRE_OK && *out) { cre_document_close(*out); *out = nullptr; }
    return result;
}
extern "C" void cre_document_close(cre_document *d) {
    if (!d) return;
    if (active == d) active = nullptr;
    delete d;
}
extern "C" int cre_document_metadata(cre_document *d, cre_metadata *out) {
    if (!d || !out) return CRE_INVALID;
    *out = d->metadata; return CRE_OK;
}
extern "C" int cre_document_layout(cre_document *d, const cre_layout *layout) {
    if (!d || !valid(layout)) return CRE_INVALID;
    return boundary([&]() -> int {
        d->view.reset(); d->chapter = UINT32_MAX;
        if (d->font_registered) { fontMan->UnregisterDocumentFonts(-1); fontMan->gc(); d->font_registered = false; }
        d->layout = *layout; return CRE_OK;
    });
}
extern "C" int cre_document_prepare(cre_document *d, const cre_anchor *anchor, uint32_t page, uint8_t *pixels, size_t capacity, cre_page_state *out) {
    if (!d || !anchor || !out || anchor->chapter >= d->metadata.chapters || !std::memchr(anchor->path, 0, sizeof(anchor->path))) return CRE_INVALID;
    int result = boundary([&]() -> int {
        int result = load(*d, anchor->chapter); if (result != CRE_OK) return result;
        d->view->Render();
        if (anchor->path[0]) {
            if (anchor->path[0] == '#') {
                if (!d->view->goLink(Utf8ToUnicode(anchor->path), false)) return CRE_CONTENT;
            } else {
                auto pointer = d->view->getDocument()->createXPointer(Utf8ToUnicode(anchor->path));
                if (pointer.isNull()) return CRE_CONTENT;
                d->view->goToBookmark(pointer);
            }
        } else if (page >= static_cast<uint32_t>(d->view->getPageCount()) || !d->view->goToPage(page)) return CRE_INVALID;
        result = render(*d, pixels, capacity, false); if (result != CRE_OK) return result;
        collectHits(*d);
        *out = {}; out->page = d->view->getCurPage(); out->pages = d->view->getPageCount(); out->complete = 1;
        out->anchor.chapter = d->chapter;
        auto path = UnicodeToUtf8(d->view->getBookmark().toString());
        if (path.length() >= static_cast<int>(sizeof(out->anchor.path))) return CRE_CONTENT;
        std::strcpy(out->anchor.path, path.c_str()); return CRE_OK;
    });
    if (result != CRE_OK) { d->view.reset(); d->chapter = UINT32_MAX; d->hits.clear(); fontMan->gc(); }
    return result;
}
extern "C" int cre_document_toc(cre_document *d, uint32_t first, cre_toc_entry *out, uint32_t capacity, uint32_t *count) {
    if (count) *count = 0;
    if (!d || !count || (!out && capacity)) return CRE_INVALID;
    return boundary([&]() -> int {
        if (d->index.ready) {
            for (uint32_t i = first; i < static_cast<uint32_t>(d->index.toc.length()) && *count < capacity; ++i) {
                auto &entry = d->index.toc[i]; auto &item = out[(*count)++]; item = {};
                item.depth = entry.depth; item.anchor.chapter = entry.chapter;
                copy(item.title, sizeof(item.title), entry.title);
                int hash = entry.href.pos(U"#");
                if (hash >= 0) {
                    lString32 id = cs32("#_doc_fragment_") + fmt::decimal(entry.chapter) + U"_ " + entry.href.substr(hash + 1);
                    copy(item.anchor.path, sizeof(item.anchor.path), id);
                }
            }
        } else if (d->view) {
            // 中文：使用显式栈读取目录，避免递归深度随恶意输入增长。
            // English: Walk contents with an explicit stack instead of input-dependent call-stack recursion.
            LVArray<LVTocItem *> pending; pending.add(d->view->getToc()); uint32_t ordinal = 0;
            while (pending.length() && *count < capacity) {
                auto *item = pending.remove(pending.length() - 1);
                if (item->getLevel() && ordinal++ >= first) {
                    auto &entry = out[(*count)++]; entry = {}; entry.depth = item->getLevel();
                    copy(entry.title, sizeof(entry.title), item->getName()); copy(entry.anchor.path, sizeof(entry.anchor.path), item->getXPointer().toString());
                }
                for (int i = item->getChildCount() - 1; i >= 0; --i) pending.add(item->getChild(i));
            }
        }
        return CRE_OK;
    });
}
extern "C" int cre_document_text(cre_document *d, char *out, size_t capacity) {
    if (!d || !d->view || !out || !capacity) return CRE_INVALID;
    return boundary([&]() -> int { copy(out, capacity, d->view->getPageText(false)); return CRE_OK; });
}
extern "C" int cre_document_cover(cre_document *d, uint8_t *pixels, size_t capacity) {
    if (!d) return CRE_INVALID;
    return boundary([&]() -> int { int result = load(*d, d->chapter == UINT32_MAX ? 0 : d->chapter); return result == CRE_OK ? render(*d, pixels, capacity, true) : result; });
}
extern "C" int cre_document_image(cre_document *d, const char *href, uint8_t *pixels, size_t capacity) {
    if (!d || !href || !*href || std::strlen(href) >= 768 || !pixels || capacity < size_t(d->layout.width / 2) * d->layout.height) return CRE_INVALID;
    return boundary([&]() -> int {
        int rc = load(*d, d->chapter == UINT32_MAX ? 0 : d->chapter); if (rc != CRE_OK) return rc;
        auto image = d->view->getDocument()->getObjectImageSource(Utf8ToUnicode(href));
        if (image.isNull() || image->GetWidth() <= 0 || image->GetHeight() <= 0) return CRE_CONTENT;
        int w = d->layout.width, h = uint64_t(image->GetHeight()) * w / image->GetWidth();
        if (h > int(d->layout.height)) { h = d->layout.height; w = uint64_t(image->GetWidth()) * h / image->GetHeight(); }
        w = std::max(1, w); h = std::max(1, h);
        std::unique_ptr<uint8_t, decltype(&cre_mcu_release)> scratch(static_cast<uint8_t *>(cre_mcu_allocate(size_t(d->layout.width) * d->layout.height)), cre_mcu_release);
        if (!scratch) return CRE_MEMORY;
        LVGrayDrawBuf buffer(d->layout.width, d->layout.height, 8, scratch.get()); buffer.Clear(0xffffff);
        buffer.Draw(image, (d->layout.width - w) / 2, (d->layout.height - h) / 2, w, h, false);
        cre_mcu_checkpoint();
        for (size_t i = 0; i < size_t(d->layout.width) * d->layout.height; i += 2)
            pixels[i / 2] = (scratch.get()[i] & 0xf0) | (scratch.get()[i + 1] >> 4);
        return CRE_OK;
    });
}
extern "C" int cre_document_hit(cre_document *d, int32_t x, int32_t y, char *href, size_t capacity, uint32_t *image) {
    if (!d || !d->view || !href || !capacity || !image || x < 0 || y < 0 || static_cast<uint32_t>(x) >= d->layout.width || static_cast<uint32_t>(y) >= d->layout.height) return CRE_INVALID;
    *href = 0; *image = 0;
    return boundary([&]() -> int {
        for (auto i = d->hits.rbegin(); i != d->hits.rend(); ++i) {
            if (x < i->x || y < i->y || x >= i->x + i->width || y >= i->y + i->height) continue;
            if (std::strlen(i->href) >= capacity) return CRE_INVALID;
            std::strcpy(href, i->href); *image = i->image; break;
        }
        return CRE_OK;
    });
}
extern "C" const cre_hit *cre_document_hits(cre_document *d, size_t *count) {
    if (!d || !count) return nullptr;
    *count = d->hits.size(); return d->hits.data();
}
extern "C" int cre_document_resolve(cre_document *d, const char *href, cre_anchor *anchor) {
    if (!d || !href || !anchor) return CRE_INVALID;
    return boundary([&]() -> int {
        *anchor = {}; anchor->chapter = d->chapter == UINT32_MAX ? 0 : d->chapter;
        if (href[0] == '#') {
            const char *fragment = "#_doc_fragment_";
            if (!std::strncmp(href, fragment, std::strlen(fragment))) {
                char *end; unsigned long chapter = std::strtoul(href + std::strlen(fragment), &end, 10);
                if (chapter >= d->metadata.chapters || (*end && *end != '_')) return CRE_CONTENT;
                anchor->chapter = chapter;
            }
            if (std::strlen(href) >= sizeof(anchor->path)) return CRE_INVALID;
            std::strcpy(anchor->path, href); return CRE_OK;
        }
        if (!d->index.ready) return CRE_CONTENT;
        lString32 target = DecodeHTMLUrlString(Utf8ToUnicode(href));
        int hash = target.pos(U"#"); lString32 path = hash >= 0 ? target.substr(0, hash) : target;
        for (int i = 0; i < d->index.chapters.length(); ++i) {
            if (path != LVCombinePaths(d->index.codeBase, d->index.chapters[i].href)) continue;
            anchor->chapter = i;
            if (hash >= 0) copy(anchor->path, sizeof(anchor->path), cs32("#_doc_fragment_") + fmt::decimal(i) + U"_ " + target.substr(hash + 1));
            return CRE_OK;
        }
        return CRE_CONTENT;
    });
}
