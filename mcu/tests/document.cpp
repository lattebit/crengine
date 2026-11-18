/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：通过纯 C 端口验证文档、锚点、排版替换及取消后的资源归还。
 * English: Verify documents, anchors, layout changes and cancellation cleanup through the C port.
 */
#include "platform.h"
#include "cre_mcu_document.h"
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
static int decodeImage(void *, const uint8_t *, size_t, uint32_t width, uint32_t height, const cre_mcu_image_memory *memory, cre_mcu_bitmap *out) {
    out->pixels = static_cast<uint8_t *>(memory->allocate(size_t(width) * height)); if (!out->pixels) return CRE_MEMORY;
    out->width = width; out->height = height; std::memset(out->pixels, 64, size_t(width) * height); return CRE_OK;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    Platform platform; auto cfg = config(platform); cfg.decode_image = decodeImage;
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    cre_layout layout{480, 640, 24, 400, 12, 100, 2, 0, 0, {}};
    std::strcpy(layout.font, argv[1]); std::vector<uint8_t> image(480 * 640 / 2);
    for (const char *extension : {"epub", "txt", "html", "fb2", "md", "rtf"}) {
        cre_document *document = nullptr;
        auto path = std::string(argv[2]) + "/fixture." + extension;
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_metadata metadata{}; assert(cre_document_metadata(document, &metadata) == CRE_OK);
        assert(metadata.format && metadata.source_bytes && metadata.chapters);
        cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        assert(state.pages > 1 && state.anchor.path[0]);
        auto first = image; size_t ink = 0; for (auto value : image) ink += value != 255;
        std::fprintf(stderr, "%s pages=%u ink=%zu\n", extension, state.pages, ink); assert(ink > 100 && ink < image.size() / 2);
        assert(cre_document_prepare(document, &anchor, 1, image.data(), image.size(), &state) == CRE_OK);
        auto second = image;
        auto saved = state.anchor;
        assert(cre_document_prepare(document, &saved, 0, image.data(), image.size(), &state) == CRE_OK);
        if (state.page != 1 || image != second) { std::fprintf(stderr, "%s restored page=%u anchor=%s\n", extension, state.page, saved.path); return 1; }
        char text[2048]; assert(cre_document_text(document, text, sizeof(text)) == CRE_OK && *text);
        platform.cancel = true;
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_CANCELLED);
        platform.cancel = false;
        assert(cre_document_layout(document, &layout) == CRE_OK);
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK && image == first);
        if (metadata.format == 1) {
            cre_toc_entry toc[4]{}; uint32_t count = 0;
            assert(cre_document_toc(document, 0, toc, 4, &count) == CRE_OK && count == 2);
            assert(toc[1].anchor.chapter == 1);
            assert(cre_document_prepare(document, &toc[1].anchor, 0, image.data(), image.size(), &state) == CRE_OK);
            assert(state.anchor.chapter == 1 && image != first);
            cre_anchor link{};
            assert(cre_document_resolve(document, "OEBPS/b.xhtml#note", &link) == CRE_OK);
            assert(cre_document_prepare(document, &link, 0, image.data(), image.size(), &state) == CRE_OK);
            assert(state.page == state.pages - 1);
            size_t count_hits = 0, reads = platform.reads;
            auto hits = cre_document_hits(document, &count_hits); assert(hits && count_hits);
            char href[768]; uint32_t is_image = 0;
            assert(cre_document_hit(document, hits[0].x + hits[0].width / 2, hits[0].y + hits[0].height / 2,
                                    href, sizeof(href), &is_image) == CRE_OK);
            assert(!std::strcmp(href, hits[0].href) && !is_image && platform.reads == reads);
        }
        cre_document_close(document); assert(!platform.files);
    }
    {
        cre_document *document = nullptr; auto path = std::string(argv[2]) + "/image.html";
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        size_t count = 0; auto hits = cre_document_hits(document, &count); assert(count && hits[0].image && *hits[0].href);
        assert(cre_document_image(document, hits[0].href, image.data(), image.size()) == CRE_OK);
        assert(std::count(image.begin(), image.end(), uint8_t(0x44)) > 10000);
        cre_document_close(document); assert(!platform.files);
    }
    {
        cre_document *document = nullptr; auto path = std::string(argv[2]) + "/cover.epub";
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_metadata metadata{}; assert(cre_document_metadata(document, &metadata) == CRE_OK && metadata.has_cover);
        assert(cre_document_cover(document, image.data(), image.size()) == CRE_OK);
        assert(std::count(image.begin(), image.end(), uint8_t(0x44)) > 10000);
        cre_mcu_stats_t stats{}; cre_mcu_statistics(&stats); assert(!stats.open_files);
        cre_document_close(document); assert(!platform.files);
    }
    for (const char *extension : {"epub", "txt", "html", "fb2", "md", "rtf"}) {
        for (size_t checkpoint : {1u, 5u, 15u, 50u, 150u, 500u, 2000u}) {
            cre_document *document = nullptr; auto path = std::string(argv[2]) + "/fixture." + extension;
            platform.cancel_at = platform.checkpoints + checkpoint;
            int result = cre_document_open(path.c_str(), &layout, &document);
            assert(result == CRE_OK || document == nullptr);
            if (result == CRE_OK) {
                cre_anchor anchor{}; cre_page_state state{};
                result = cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state);
            }
            assert(result == CRE_OK || result == CRE_CANCELLED || result == CRE_CONTENT);
            platform.cancel_at = SIZE_MAX; cre_document_close(document);
            if (platform.files) { std::fprintf(stderr, "cancel extension=%s checkpoint=%zu result=%d files=%zu\n", extension, checkpoint, result, platform.files); return 1; }
        }
    }
    for (size_t checkpoint : {2u, 20u, 100u, 2000u}) {
        cre_document *document = nullptr; auto path = std::string(argv[2]) + "/fixture.epub";
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_anchor anchor{}; cre_page_state state{};
        platform.cancel_at = platform.checkpoints + checkpoint;
        int result = cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state);
        assert(result == CRE_CANCELLED || result == CRE_OK);
        platform.cancel_at = SIZE_MAX;
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        assert(state.pages > 1 && state.page == 0);
        cre_document_close(document); assert(!platform.files);
    }
    for (const char *extension : {"epub", "txt", "html", "fb2", "md", "rtf"}) {
        for (size_t read : {1u, 5u, 15u, 50u, 150u}) {
            cre_document *document = nullptr; auto path = std::string(argv[2]) + "/fixture." + extension;
            platform.fail_read_at = platform.reads + read;
            int result = cre_document_open(path.c_str(), &layout, &document);
            assert(result == CRE_OK || document == nullptr);
            if (result == CRE_OK) {
                cre_anchor anchor{}; cre_page_state state{};
                result = cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state);
            }
            assert(result == CRE_OK || result == CRE_IO);
            platform.fail_read_at = SIZE_MAX; cre_document_close(document); assert(!platform.files);
        }
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
    std::puts("Document C port passed");
}
