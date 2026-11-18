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

struct cre_document {
    std::string path;
    cre_layout layout{};
    cre_metadata metadata{};
    EpubIndex index;
    LVStreamRef stream;
    std::unique_ptr<LVDocView> view;
    uint32_t chapter = UINT32_MAX;
    bool font_registered = false;
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
        *out = {}; out->page = d->view->getCurPage(); out->pages = d->view->getPageCount(); out->complete = 1;
        out->anchor.chapter = d->chapter;
        auto path = UnicodeToUtf8(d->view->getBookmark().toString());
        if (path.length() >= static_cast<int>(sizeof(out->anchor.path))) return CRE_CONTENT;
        std::strcpy(out->anchor.path, path.c_str()); return CRE_OK;
    });
    if (result != CRE_OK) { d->view.reset(); d->chapter = UINT32_MAX; fontMan->gc(); }
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
extern "C" int cre_document_hit(cre_document *d, int32_t x, int32_t y, char *href, size_t capacity, uint32_t *image) {
    if (!d || !d->view || !href || !capacity || !image || x < 0 || y < 0 || static_cast<uint32_t>(x) >= d->layout.width || static_cast<uint32_t>(y) >= d->layout.height) return CRE_INVALID;
    *href = 0; *image = 0;
    return boundary([&]() -> int {
        auto pointer = d->view->getNodeByPoint(lvPoint(x, y), true);
        if (pointer.isNull()) return CRE_OK;
        if (pointer.isImage()) { *image = 1; copy(href, capacity, pointer.getNode()->getAttributeValue("src")); }
        else copy(href, capacity, pointer.getHRef());
        return CRE_OK;
    });
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
