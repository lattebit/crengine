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

int main(int argc, char **argv) {
    assert(argc == 3);
    Platform platform; auto cfg = config(platform);
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    unicodeBoundaries();
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
}
