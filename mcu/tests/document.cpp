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
#include <map>
struct Sources {
    std::map<uint64_t, std::vector<uint8_t>> records;
    unsigned hits = 0, writes = 0;
    bool fail_write = false;
    static int read(void *context, uint64_t key, void *out, size_t bytes) {
        auto &s = *static_cast<Sources *>(context); auto found = s.records.find(key);
        if (found == s.records.end() || found->second.size() != bytes) return false;
        std::memcpy(out, found->second.data(), bytes); ++s.hits; return true;
    }
    static int write(void *context, uint64_t key, const void *data, size_t bytes) {
        auto &s = *static_cast<Sources *>(context); if (s.fail_write) return false;
        auto *p = static_cast<const uint8_t *>(data); s.records[key].assign(p, p + bytes); ++s.writes; return true;
    }
};
static int decodeImage(void *, const uint8_t *, size_t, uint32_t width, uint32_t height, const cre_mcu_image_memory *memory, cre_mcu_bitmap *out) {
    out->pixels = static_cast<uint8_t *>(memory->allocate(size_t(width) * height)); if (!out->pixels) return CRE_MEMORY;
    out->width = width; out->height = height; std::memset(out->pixels, 64, size_t(width) * height); return CRE_OK;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    Platform platform; auto cfg = config(platform); cfg.decode_image = decodeImage;
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    cre_layout layout{480, 640, 24, 400, 12, 100, 2, 0, 0, {}, 12};
    std::strcpy(layout.font, argv[1]); std::vector<uint8_t> image(480 * 640 / 2);
    for (const char *language : {"english", "chinese", "traditional"}) {
        auto path = std::string(argv[2]) + "/headings-" + language + ".txt";
        cre_document *document = nullptr; assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_toc_entry entries[4]{}; uint32_t count = 0;
        assert(cre_document_toc(document, 0, entries, 4, &count) == CRE_OK);
        const char *first = !std::strcmp(language, "english") ? "Chapter 1 Start" : !std::strcmp(language, "chinese") ? "第一章 开始" : "第壹卷 開始";
        const char *last = !std::strcmp(language, "english") ? "Chapter 2 End" : !std::strcmp(language, "chinese") ? "第二章 结束" : "第２節 結束";
        if (count != 2 || std::strcmp(entries[0].title, first) || std::strcmp(entries[1].title, last))
            std::fprintf(stderr, "TXT %s contents=%u first=%s last=%s\n", language, count, entries[0].title, entries[1].title);
        assert(count == 2 && !std::strcmp(entries[0].title, first) && !std::strcmp(entries[1].title, last));
        cre_page_state page{};
        assert(cre_document_prepare(document, &entries[1].anchor, 0, image.data(), image.size(), &page) == CRE_OK);
        char text[512]{}; assert(cre_document_text(document, text, sizeof(text)) == CRE_OK && std::strstr(text, last));
        cre_document_close(document); assert(!platform.files);
    }
    {
        auto path = std::string(argv[2]) + "/many-headings.txt";
        cre_document *document = nullptr; assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        size_t reads = platform.reads;
        cre_toc_entry entries[16]{}; uint32_t count = 0, total = 0;
        do {
            assert(cre_document_toc(document, total, entries, 16, &count) == CRE_OK);
            for (uint32_t i = 0; i < count; ++i) {
                auto expected = "Chapter " + std::to_string(total + i);
                assert(entries[i].title == expected && entries[i].anchor.path[0]);
            }
            total += count;
        } while (count);
        assert(total == 2200);
        assert(cre_document_toc(document, 1098, entries, 4, &count) == CRE_OK && count == 4);
        assert(!std::strcmp(entries[0].title, "Chapter 1098") && !std::strcmp(entries[3].title, "Chapter 1101"));
        assert(platform.reads == reads);
        cre_document_close(document); assert(!platform.files);
    }
    {
        std::vector<uint8_t> reference;
        for (const char *encoding : {"utf-8", "gbk", "utf-16", "utf-32"}) {
            cre_document *document = nullptr;
            auto path = std::string(argv[2]) + "/short-" + encoding + ".txt";
            assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
            cre_anchor anchor{}; cre_page_state state{}; char text[128]{};
            assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
            assert(cre_document_text(document, text, sizeof(text)) == CRE_OK && std::strstr(text, "中文"));
            if (reference.empty()) reference = image; else assert(reference == image);
            cre_document_close(document); assert(!platform.files);
        }
    }
    const char *languages[] = {"ja", "", "ja", "ja", "en", "ja", "", "ja", "en-US", "en-GB"};
    for (unsigned i = 0; i < sizeof(languages) / sizeof(*languages); ++i) {
        cre_document *document = nullptr;
        std::string path = std::string(argv[2]) + "/language-" + std::to_string(i) + ".epub";
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_metadata data{}; assert(cre_document_metadata(document, &data) == CRE_OK);
        if (std::strcmp(data.language, languages[i])) std::fprintf(stderr, "language case=%u expected=%s actual=%s\n", i, languages[i], data.language);
        assert(!std::strcmp(data.language, languages[i]));
        cre_document_close(document); assert(!platform.files);
    }
    {
        Sources sources; cre_source_cache cache{&sources, Sources::read, Sources::write};
        auto path = std::string(argv[2]) + "/fixture.epub"; std::vector<uint8_t> original;
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            cre_document *document = nullptr; assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
            assert(cre_document_source_cache(document, &cache) == CRE_OK);
            cre_anchor anchor{}; cre_page_state state{};
            assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
            if (!attempt) {
                original = image; assert(!sources.writes && !sources.hits);
                // 中文：邻章预读不改变正文、命中区域或当前位置；热命中不再读取源文件。
                // English: Neighbor source prefetch preserves body, hit areas and position; warm hits avoid source reads.
                char before[2048], after[2048];
                assert(cre_document_text(document, before, sizeof(before)) == CRE_OK);
                assert(cre_document_prefetch(document, 2) == CRE_INVALID);
                platform.fail_read = true;
                assert(cre_document_prefetch(document, 1) == CRE_IO);
                platform.fail_read = false;
                assert(cre_document_text(document, after, sizeof(after)) == CRE_OK && !std::strcmp(before, after));
                platform.cancel = true;
                assert(cre_document_prefetch(document, 1) == CRE_CANCELLED);
                platform.cancel = false;
                assert(cre_document_prefetch(document, 1) == CRE_OK && image == original);
                auto reads = platform.reads;
                platform.fail_read = true;
                assert(cre_document_prefetch(document, 1) == CRE_OK && platform.reads == reads);
                platform.fail_read = false;
                assert(cre_document_text(document, after, sizeof(after)) == CRE_OK && !std::strcmp(before, after));
                assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK && image == original);
                sources.fail_write = true; assert(cre_document_checkpoint(document) == CRE_OK && !sources.writes);
                sources.fail_write = false; assert(cre_document_checkpoint(document) == CRE_OK && sources.writes);
            } else assert(image == original && sources.hits);
            unsigned writes = sources.writes;
            assert(cre_document_checkpoint(document) == CRE_OK && sources.writes == writes);
            // 中文：更改排版后复用源数据；不新增磁盘读取或重复写入。
            // English: Reuse sources after layout changes without additional disk-cache reads or duplicate writes.
            unsigned hits = sources.hits;
            assert(cre_document_layout(document, &layout) == CRE_OK);
            assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK && image == original);
            assert(sources.hits == hits && sources.writes == writes);
            cre_document_close(document); assert(!platform.files);
        }
    }
    {
        Sources sources; cre_source_cache cache{&sources, Sources::read, Sources::write};
        auto path = std::string(argv[2]) + "/prefetch-capacity.epub";
        cre_document *document = nullptr; assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        assert(cre_document_source_cache(document, &cache) == CRE_OK);
        assert(cre_document_prefetch(document, 0) == CRE_OK && cre_document_prefetch(document, 1) == CRE_OK);
        cre_mcu_stats_t before{}, after{}; cre_mcu_statistics(&before);
        auto reads = platform.reads;
        // 中文：容量不足或章节过大时跳过，既不读取新源也不淘汰已有两项。
        // English: Full capacity and oversized chapters skip without reading new sources or evicting either hot entry.
        platform.fail_read = true;
        assert(cre_document_prefetch(document, 2) == CRE_OK && cre_document_prefetch(document, 3) == CRE_OK);
        assert(cre_document_prefetch(document, 0) == CRE_OK && cre_document_prefetch(document, 1) == CRE_OK);
        cre_mcu_statistics(&after); assert(platform.reads == reads && after.used == before.used);
        platform.fail_read = false;
        assert(cre_document_checkpoint(document) == CRE_OK && sources.writes == 2);
        cre_document_close(document); assert(!platform.files);
    }
    {
        std::vector<uint8_t> reference;
        for (const char *encoding : {"utf-8", "utf-16-le", "utf-16-be"}) {
            auto path = std::string(argv[2]) + "/xml-" + encoding + ".epub";
            cre_document *document = nullptr;
            int rc = cre_document_open(path.c_str(), &layout, &document);
            if (rc != CRE_OK) std::fprintf(stderr, "XML encoding %s open=%d\n", encoding, rc);
            assert(rc == CRE_OK);
            cre_anchor anchor{}; cre_page_state state{};
            assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
            if (reference.empty()) reference = image; else assert(reference == image);
            cre_document_close(document); assert(!platform.files);
        }
    }
    {
        // 中文：CSS 自定义行高只应用一次，200% 的 24px 文本行距应为 48px。
        // English: Apply custom CSS line height once: 200% of 24px must advance by 48px.
        auto custom = layout; custom.override_style = 1; custom.line_percent = 200; custom.paragraph_quarters = 0;
        auto path = std::string(argv[2]) + "/line-height.html";
        cre_document *document = nullptr; assert(cre_document_open(path.c_str(), &custom, &document) == CRE_OK);
        cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        std::vector<int> starts; bool ink_before = false;
        for (int y = 0; y < 640; ++y) {
            bool ink = false;
            for (int x = 0; x < 240; ++x) ink |= image[y * 240 + x] != 255;
            if (ink && !ink_before) starts.push_back(y);
            ink_before = ink;
        }
        assert(starts.size() > 4 && starts[1] - starts[0] == 48 && starts[2] - starts[1] == 48);
        // 中文：底边距独立配置；取消预留空间能绘制更多完整行，左右边距保持。
        // English: An independent bottom margin releases space for complete lines while side margins stay intact.
        custom.bottom_margin = 96;
        assert(cre_document_layout(document, &custom) == CRE_OK);
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        auto reserved = std::count_if(image.begin() + 540 * 240, image.end(), [](uint8_t b) { return b != 255; });
        custom.bottom_margin = 0;
        assert(cre_document_layout(document, &custom) == CRE_OK);
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        assert(std::count_if(image.begin() + 540 * 240, image.end(), [](uint8_t b) { return b != 255; }) > reserved);
        cre_document_close(document); assert(!platform.files);
    }
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
        uint32_t confirmed = state.pages, complete = state.complete;
        assert(cre_document_extend(document, confirmed + 1, &confirmed, &complete) == CRE_OK && image == first);
        if (!complete) {
            platform.cancel_at = platform.checkpoints + 2;
            assert(cre_document_extend(document, 0, &confirmed, &complete) == CRE_CANCELLED && image == first);
            platform.cancel_at = SIZE_MAX;
            assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK && image == first);
        }
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
        // 中文：1em 行内图片应与 24px 字号等高，不能被旧式图片放大选项翻倍。
        // English: A 1em inline image matches the 24px font size without legacy image zoom doubling it.
        cre_document *document = nullptr; auto path = std::string(argv[2]) + "/inline-symbol.html";
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        size_t count = 0; auto hits = cre_document_hits(document, &count);
        assert(count == 1 && hits[0].image && hits[0].width == 32);
        assert(std::count(image.begin(), image.end(), uint8_t(0x44)) >= 31 * 24 / 2);
        assert(std::count(image.begin(), image.end(), uint8_t(0x44)) <= 32 * 24 / 2);
        cre_document_close(document); assert(!platform.files);
    }
    for (const char *name : {"image.html", "svg.html"}) {
        cre_document *document = nullptr; auto path = std::string(argv[2]) + "/" + name;
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_anchor anchor{}; cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, image.data(), image.size(), &state) == CRE_OK);
        size_t count = 0; auto hits = cre_document_hits(document, &count); assert(count && hits[0].image && *hits[0].href && hits[0].width == 128);
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
