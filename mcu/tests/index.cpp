/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：大 EPUB 索引、末章定位和批次读取不依赖全书正文加载。
 * English: Large EPUB indexing, last-chapter navigation and batch reads without loading the entire body.
 */
#include "platform.h"
#include "cre_mcu_document.h"
#include "lvstream.h"
#include <chrono>
#include <cstring>
#include <string>
#include <vector>
int main(int argc, char **argv) {
    assert(argc == 3);
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    assert(LVExtractPath(lString32("nav.xhtml")) == U"./");
    assert(LVDetectPathDelimiter(lString32("text")) == '/' && LVDetectPathDelimiter(lString8("text")) == '/');
    cre_layout layout{320, 480, 24, 400, 8, 100, 0, 0, 0, {}, 8};
    assert(std::strlen(argv[1]) < sizeof(layout.font)); std::strcpy(layout.font, argv[1]);
    std::string path = std::string(argv[2]) + "/large-index.epub";
    cre_document *document = nullptr;
    auto start = std::chrono::steady_clock::now();
    size_t checkpoints = platform.checkpoints;
    assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
    checkpoints = platform.checkpoints - checkpoints;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    cre_metadata metadata{}; assert(cre_document_metadata(document, &metadata) == CRE_OK && metadata.chapters == 30000);
    size_t before = platform.reads;
    cre_toc_entry batch[16]{}; uint32_t count = 0;
    for (unsigned first : {0u, 14992u, 29984u}) {
        int result = cre_document_toc(document, first, batch, 16, &count);
        if (result != CRE_OK || count != 16) std::fprintf(stderr, "TOC first=%u count=%u result=%d index_ms=%lld\n", first, count, result, (long long)elapsed);
        assert(result == CRE_OK && count == 16);
        for (unsigned i = 0; i < count; ++i) {
            auto title = "Chapter " + std::to_string(first + i);
            assert(batch[i].anchor.chapter == first + i && batch[i].depth == 1 && batch[i].title == title && batch[i].anchor.path[0]);
        }
    }
    assert(platform.reads == before);
    std::vector<uint8_t> pixels(320 * 480 / 2); cre_page_state state{};
    assert(cre_document_prepare(document, &batch[15].anchor, 0, pixels.data(), pixels.size(), &state) == CRE_OK);
    char text[256]; assert(cre_document_text(document, text, sizeof(text)) == CRE_OK && std::strstr(text, "Body chapter 29999."));
    cre_document_close(document); document = nullptr;
    platform.cancel_at = platform.checkpoints + checkpoints - 5;
    assert(cre_document_open(path.c_str(), &layout, &document) == CRE_CANCELLED && !document && !platform.files);
    platform.cancel_at = SIZE_MAX;
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    std::printf("PASS 30000-entry EPUB, middle/last TOC batches and last chapter; index_ms=%lld\n", (long long)elapsed);
}
