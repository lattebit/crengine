/***************************************************************************
 *   crengine-ng                                                           *
 *   Copyright (C) 2022,2024 Aleksey Chernov <valexlin@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License           *
 *   as published by the Free Software Foundation; either version 2        *
 *   of the License, or (at your option) any later version.                *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,            *
 *   MA 02110-1301, USA.                                                   *
 ***************************************************************************/

#include "crsetup.h"

#if (USE_MD4C == 1)

#include "lvtinydom.h"
#include "mdfmt.h"

#include <string.h>

#include <md4c.h>
#include <exception>
#include <stdexcept>

namespace {
// 中文：直接将 MD4C 事件写入 DOM，仅保留源文本，避免完整 HTML 副本。
// English: Write MD4C events directly into the DOM, retaining only source text without a full HTML copy.
struct MarkdownWriter {
    ldomDocumentWriter writer;
    std::exception_ptr failure;
    unsigned imageDepth = 0;
    lString32 imageAlt;
    explicit MarkdownWriter(ldomDocument *doc) : writer(doc) {}
    void open(const lChar32 *tag) { writer.OnTagOpen(U"", tag); }
    void body() { writer.OnTagBody(); }
    void close(const lChar32 *tag) { writer.OnTagClose(U"", tag); }
    void attr(const lChar32 *name, const MD_ATTRIBUTE &a) {
        lString32 value = Utf8ToUnicode(lString8(a.text, a.size));
        PreProcessXmlString(value, TXTFLG_PROCESS_ATTRIBUTE);
        writer.OnAttribute(U"", name, value.c_str());
    }
    void empty(const lChar32 *tag) { open(tag); body(); close(tag); }
};
const lChar32 *blockTag(MD_BLOCKTYPE type, void *detail) {
    switch (type) {
    case MD_BLOCK_DOC: return U"body";
    case MD_BLOCK_QUOTE: return U"blockquote";
    case MD_BLOCK_UL: return U"ul";
    case MD_BLOCK_OL: return U"ol";
    case MD_BLOCK_LI: return U"li";
    case MD_BLOCK_HR: return U"hr";
    case MD_BLOCK_H: {
        static const lChar32 *tags[] = {U"h1", U"h2", U"h3", U"h4", U"h5", U"h6"};
        return tags[static_cast<MD_BLOCK_H_DETAIL *>(detail)->level - 1];
    }
    case MD_BLOCK_CODE: return U"pre";
    case MD_BLOCK_P: return U"p";
    case MD_BLOCK_TABLE: return U"table";
    case MD_BLOCK_THEAD: return U"thead";
    case MD_BLOCK_TBODY: return U"tbody";
    case MD_BLOCK_TR: return U"tr";
    case MD_BLOCK_TH: return U"th";
    case MD_BLOCK_TD: return U"td";
    default: return nullptr;
    }
}
const lChar32 *spanTag(MD_SPANTYPE type) {
    switch (type) {
    case MD_SPAN_EM: return U"em";
    case MD_SPAN_STRONG: return U"strong";
    case MD_SPAN_A: return U"a";
    case MD_SPAN_IMG: return U"img";
    case MD_SPAN_CODE: return U"code";
    case MD_SPAN_DEL: return U"del";
    case MD_SPAN_U: return U"u";
    default: return U"span";
    }
}
template<class F> int guarded(void *ctx, F f) {
    auto &w = *static_cast<MarkdownWriter *>(ctx);
    if (w.failure) return 1;
    try { f(w); return 0; }
    catch (...) { w.failure = std::current_exception(); return 1; }
}
int enterBlock(MD_BLOCKTYPE type, void *detail, void *ctx) {
    return guarded(ctx, [&](MarkdownWriter &w) {
        const auto *tag = blockTag(type, detail);
        if (!tag) return;
        w.open(tag);
        if (type == MD_BLOCK_OL) {
            lString32 start = lString32::itoa(static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start);
            w.writer.OnAttribute(U"", U"start", start.c_str());
        }
        if (type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
            static const lChar32 *align[] = {U"", U"left", U"center", U"right"};
            auto value = static_cast<MD_BLOCK_TD_DETAIL *>(detail)->align;
            if (value != MD_ALIGN_DEFAULT) w.writer.OnAttribute(U"", U"align", align[value]);
        }
        w.body();
        if (type == MD_BLOCK_CODE) { w.open(U"code"); w.body(); }
        if (type == MD_BLOCK_LI) {
            auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
            if (d->is_task) {
                const lChar32 *mark = d->task_mark == ' ' ? U"☐ " : U"☑ ";
                w.writer.OnText(mark, 2, 0);
            }
        }
    });
}
int leaveBlock(MD_BLOCKTYPE type, void *detail, void *ctx) {
    return guarded(ctx, [&](MarkdownWriter &w) {
        if (type == MD_BLOCK_CODE) w.close(U"code");
        const auto *tag = blockTag(type, detail);
        if (tag) w.close(tag);
    });
}
int enterSpan(MD_SPANTYPE type, void *detail, void *ctx) {
    return guarded(ctx, [&](MarkdownWriter &w) {
        if (w.imageDepth) { if (type == MD_SPAN_IMG) ++w.imageDepth; return; }
        w.open(spanTag(type));
        if (type == MD_SPAN_A) {
            auto *d = static_cast<MD_SPAN_A_DETAIL *>(detail);
            w.attr(U"href", d->href); w.attr(U"title", d->title);
        }
        if (type == MD_SPAN_IMG) {
            auto *d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
            w.attr(U"src", d->src); w.attr(U"title", d->title);
            w.imageDepth = 1; w.imageAlt.clear(); return;
        }
        w.body();
    });
}
int leaveSpan(MD_SPANTYPE type, void *, void *ctx) {
    return guarded(ctx, [&](MarkdownWriter &w) {
        if (w.imageDepth) {
            if (type != MD_SPAN_IMG || --w.imageDepth) return;
            w.writer.OnAttribute(U"", U"alt", w.imageAlt.c_str()); w.body();
        }
        w.close(spanTag(type));
    });
}
// 中文：原始 HTML 片段借用同一 writer，不开启或结束第二份文档。
// English: Raw HTML fragments borrow the same writer without starting or ending another document.
class HTMLFragment : public LVXMLParserCallback {
    ldomDocumentWriter &w;
public:
    explicit HTMLFragment(ldomDocumentWriter &target) : w(target) {}
    bool OnBlob(lString32 name, const lUInt8 *data, int size) override { return w.OnBlob(name, data, size); }
    void OnStart(LVFileFormatParser *) override {}
    void OnStop() override {}
    lUInt32 getFlags() override { return w.getFlags(); }
    void setFlags(lUInt32 flags) override { w.setFlags(flags); }
    void OnEncoding(const lChar32 *, const lChar32 *) override {}
    ldomNode *OnTagOpen(const lChar32 *ns, const lChar32 *tag) override { return w.OnTagOpen(ns, tag); }
    void OnTagBody() override { w.OnTagBody(); }
    void OnTagClose(const lChar32 *ns, const lChar32 *tag, bool self = false) override { w.OnTagClose(ns, tag, self); }
    void OnAttribute(const lChar32 *ns, const lChar32 *name, const lChar32 *value) override { w.OnAttribute(ns, name, value); }
    void OnText(const lChar32 *text, int size, lUInt32 flags) override { w.OnText(text, size, flags); }
};
int textEvent(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ctx) {
    return guarded(ctx, [&](MarkdownWriter &w) {
        if (type == MD_TEXT_HTML && !w.imageDepth) {
            HTMLFragment fragment(w.writer);
            LVStreamRef stream = LVCreateMemoryStream(const_cast<MD_CHAR *>(text), size, true);
            LVHTMLParser parser(stream, &fragment);
            parser.SetCharset(U"utf-8");
            if (!parser.Parse()) throw std::runtime_error("Markdown HTML fragment");
            return;
        }
        if (type == MD_TEXT_BR && !w.imageDepth) { w.empty(U"br"); return; }
        lString32 value = type == MD_TEXT_NULLCHAR ? lString32(U"�") :
                          type == MD_TEXT_SOFTBR || type == MD_TEXT_BR ? lString32(U"\n") :
                          Utf8ToUnicode(lString8(text, size));
        if (type == MD_TEXT_ENTITY) PreProcessXmlString(value, TXTFLG_PRE);
        if (w.imageDepth) w.imageAlt += value;
        else w.writer.OnText(value.c_str(), value.length(), type == MD_TEXT_CODE ? TXTFLG_PRE : 0);
    });
}
}

bool DetectMarkdownFormat(LVStreamRef stream, const lString32 &fileName) {
    lString32 name(fileName); name.lowercase();
    if (!name.endsWith(".md") && !name.endsWith(".markdown")) return false;
    lvsize_t size = stream->GetSize();
    return size > 0 && size <= MARKDOWN_MAX_FILE_SIZE;
}

bool ImportMarkdownDocument(LVStreamRef stream, const lString32 &fileName, ldomDocument *doc,
                            LVDocViewCallback *progressCallback, CacheLoadingCallback *formatCallback) {
    if (doc->openFromCache(formatCallback)) {
        if (progressCallback) progressCallback->OnLoadFileEnd();
        return true;
    }
    lvsize_t length = stream->GetSize();
    if (length > MARKDOWN_MAX_FILE_SIZE) return false;
    lString8 source;
    char chunk[1024];
    stream->SetPos(0);
    while (static_cast<lvsize_t>(source.length()) < length) {
        lvsize_t bytes = 0;
        if (stream->Read(chunk, sizeof(chunk), &bytes) != LVERR_OK || !bytes) return false;
        source.append(chunk, bytes);
    }
    MarkdownWriter w(doc);
    w.writer.OnStart(nullptr);
    w.open(U"html"); w.body();
    MD_PARSER parser{};
    parser.flags = MD_FLAG_COLLAPSEWHITESPACE | MD_DIALECT_GITHUB;
    parser.enter_block = enterBlock; parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan; parser.leave_span = leaveSpan; parser.text = textEvent;
    int result = md_parse(source.c_str(), source.length(), &parser, &w);
    if (w.failure) std::rethrow_exception(w.failure);
    if (result != 0) return false;
    w.close(U"html"); w.writer.OnStop();
    doc->getProps()->setString(DOC_PROP_TITLE, LVExtractFilenameWithoutExtension(fileName));
    doc->buildTocFromHeadings();
    if (progressCallback) progressCallback->OnLoadFileEnd();
    return true;
}
#endif
