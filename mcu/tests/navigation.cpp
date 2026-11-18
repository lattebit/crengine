/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：独立索引必须保留嵌套目录、相对路径和 Unicode 编码，再定位到真实正文。
 * English: Independent indexes retain nested contents, relative paths and Unicode encodings, then locate real body text.
 */
#include "platform.h"
#include "cre_mcu_document.h"
#include "lvtinydom.h"
#include <cstring>
#include <string>
#include <vector>
#include <memory>

static void unicodeBoundaries() {
    for (bool big : {false, true}) {
        for (unsigned boundary : {8190u, 8192u, 16382u, 16384u}) {
            std::u16string source = u"<?xml version=\"1.0\" encoding=\"UTF-16\"?><root><p>";
            size_t padding = boundary / 2 - source.size(); source.append(padding, u'A');
            source += u"\U0001f600"; source += u"B\xd800"; source += u"C\xdc00"; source += u"D</p></root>";
            std::vector<uint8_t> bytes;
            for (auto unit : source) { bytes.push_back(big ? unit >> 8 : unit & 255); bytes.push_back(big ? unit & 255 : unit >> 8); }
            std::unique_ptr<ldomDocument> document(LVParseXMLStream(LVCreateMemoryStream(bytes.data(), bytes.size(), false)));
            assert(document); auto *node = document->nodeFromXPath(lString32("root/p")); assert(node);
            auto text = node->getText();
            assert(text.length() == int(padding + 6));
            assert(text.substr(padding) == U"\U0001f600B\ufffdC\ufffdD");
        }
    }
}

static void xmlEntities() {
    // 中文：在读取边界拆开多字节属性与文本，保留实体和 CDATA 的不同语义。
    // English: Split multibyte attributes/text across read boundaries while retaining distinct entity and CDATA semantics.
    for (unsigned boundary : {8190u, 8192u, 16382u, 16384u}) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root>";
        xml.append(boundary - xml.size(), ' ');
        xml += "<p title='中😀 &nbsp;X'>A&amp;B<![CDATA[&nbsp;]]>中文😀</p></root>";
        std::unique_ptr<ldomDocument> doc(LVParseXMLStream(LVCreateMemoryStream(xml.data(), xml.size(), false)));
        assert(doc); auto *p = doc->nodeFromXPath(lString32("root/p")); assert(p);
        assert(p->getAttributeValue("title") == U"中😀 \u00a0X");
        assert(p->getText() == U"A&B&nbsp;中文😀");
    }
}

static void malformedPackages(Platform &platform, const char *directory, const cre_layout &layout) {
    auto cfg = config(platform); auto normal_open = cfg.open;
    struct Restricted { Platform *platform; decltype(normal_open) open; std::string book; const char *font; } access{&platform, normal_open, {}, layout.font};
    // 中文：索引和正文资源只许来自归档，外部实体和路径不得转为宿主文件读取。
    // English: Index/body resources must come from the archive; entities and paths may not become host file reads.
    cfg.context = &access;
    cfg.open = [](void *context, const char *path, uint64_t *bytes) -> void * {
        auto &access = *static_cast<Restricted *>(context);
        assert(access.book == path || !std::strcmp(path, access.font));
        return access.open(access.platform, path, bytes);
    };
    cfg.allocate = [](void *c, size_t b) { return allocate(static_cast<Restricted *>(c)->platform, b); };
    cfg.release = [](void *c, void *p) { release(static_cast<Restricted *>(c)->platform, p); };
    cfg.read_at = [](void *c, void *f, uint64_t o, void *p, size_t b) { return readAt(static_cast<Restricted *>(c)->platform, f, o, p, b); };
    cfg.close = [](void *c, void *f) { closeFile(static_cast<Restricted *>(c)->platform, f); };
    cfg.cancelled = [](void *c) { return cancelled(static_cast<Restricted *>(c)->platform); };
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    std::vector<uint8_t> pixels(layout.width * layout.height / 2), expected;
    auto clean = [&] {
        access.book = std::string(directory) + "/nav-ncx-utf-8.epub";
        cre_document *doc = nullptr; cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_open(access.book.c_str(), &layout, &doc) == CRE_OK);
        assert(cre_document_prepare(doc, &anchor, 0, pixels.data(), pixels.size(), &state) == CRE_OK);
        if (expected.empty()) expected = pixels; else assert(expected == pixels);
        cre_document_close(doc); assert(!platform.files);
    };
    clean();
    for (const char *name : {"missing-reference", "duplicate-id", "nul", "mismatched-tag", "multiple-roots",
             "broken-ncx", "trailing-ncx", "internal-dtd", "truncated-utf16", "surrogate-utf16", "empty-container",
             "escape-container", "escape-path", "url-path", "escaped-url-path", "bad-percent"}) {
        access.book = std::string(directory) + "/package-" + name + ".epub";
        cre_document *doc = nullptr; int rc = cre_document_open(access.book.c_str(), &layout, &doc);
        assert(rc == CRE_OK || rc == CRE_CONTENT);
        if (rc == CRE_OK) {
            cre_metadata metadata{}; assert(cre_document_metadata(doc, &metadata) == CRE_OK);
            assert(metadata.chapters && metadata.chapters <= 4);
            cre_anchor anchor{}; cre_page_state state{};
            rc = cre_document_prepare(doc, &anchor, 0, pixels.data(), pixels.size(), &state);
            assert(rc == CRE_OK || rc == CRE_CONTENT || rc == CRE_IO);
        } else assert(!doc);
        cre_document_close(doc); assert(!platform.files);
        clean();
    }
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    Platform platform; auto cfg = config(platform);
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    unicodeBoundaries(); xmlEntities();
    cre_layout layout{320, 480, 24, 400, 8, 100, 0, 0, 0, {}, 8};
    assert(std::strlen(argv[1]) < sizeof(layout.font)); std::strcpy(layout.font, argv[1]);
    std::vector<uint8_t> pixels(320 * 480 / 2);
    for (const char *kind : {"ncx", "nav", "fallback"}) {
        for (const char *encoding : {"utf-8", "utf-16-le", "utf-16-be"}) {
            std::string path = std::string(argv[2]) + "/nav-" + kind + "-" + encoding + ".epub";
            cre_document *document = nullptr;
            int rc = cre_document_open(path.c_str(), &layout, &document);
            std::fprintf(stderr, "%s open=%d\n", path.c_str(), rc); assert(rc == CRE_OK);
            cre_metadata metadata{}; assert(cre_document_metadata(document, &metadata) == CRE_OK);
            assert(metadata.chapters == 4 && !std::strcmp(metadata.title, "Title & 中文"));
            cre_toc_entry entries[4]{}; uint32_t count = 0;
            assert(cre_document_toc(document, 0, entries, 4, &count) == CRE_OK && count == 4);
            assert(!std::strcmp(entries[0].title, "Parent & 中文😀"));
            assert(!std::strcmp(entries[1].title, "Child Two"));
            for (uint32_t i = 0; i < 4; ++i) {
                assert(entries[i].anchor.chapter == i && entries[i].depth == (i == 1 ? 2u : 1u));
                cre_page_state state{};
                assert(cre_document_prepare(document, &entries[i].anchor, 0, pixels.data(), pixels.size(), &state) == CRE_OK);
                char text[2048]; assert(cre_document_text(document, text, sizeof(text)) == CRE_OK);
                auto expected = "Body chapter " + std::to_string(i + 1);
                assert(state.anchor.chapter == i && std::strstr(text, expected.c_str()));
            }
            assert(cre_document_toc(document, 4, entries, 4, &count) == CRE_OK && !count);
            cre_document_close(document); assert(!platform.files);
        }
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    malformedPackages(platform, argv[2], layout);
}
