/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：通过公共 C 端口验证布局恢复、故障回退和排版隔离。
 * English: Verify layout restoration, fault fallback and typography isolation through the public C port.
 */
#include "platform.h"
#include "cache_records.h"
#include "cre_mcu_document.h"
#include <string>
static Records records;
static unsigned hits;
static int cachedRead(void *, uint64_t key, void *out, size_t capacity, size_t *actual) {
    bool hit = read(&records, key, out, capacity, actual); hits += hit; return hit;
}
static int cachedWrite(void *, uint64_t key, const void *data, size_t bytes) { return write(&records, key, data, bytes); }
static void cachedRemove(void *, uint64_t key) { remove(&records, key); }
static cre_document *open(const std::string &path, const cre_layout &layout) {
    cre_document *document = nullptr; int result = cre_document_open(path.c_str(), &layout, &document);
    if (result) std::fprintf(stderr, "open failed=%d %s\n", result, path.c_str());
    assert(result == CRE_OK && document); return document;
}
static cre_page_state prepare(cre_document *document, std::vector<uint8_t> &pixels, unsigned page) {
    cre_anchor anchor{}; cre_page_state state{};
    int result = cre_document_prepare(document, &anchor, page, pixels.data(), pixels.size(), &state);
    if (result) std::fprintf(stderr, "prepare failed=%d page=%u\n", result, page);
    assert(result == CRE_OK && state.page == page); return state;
}
int main(int argc, char **argv) {
    assert(argc == 3); Platform platform; auto cfg = config(platform); cfg.memory_limit = 6 * 1024 * 1024;
    cfg.read_layout = cachedRead; cfg.write_layout = cachedWrite; cfg.remove_layout = cachedRemove;
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    cre_layout layout{480, 640, 24, 400, 12, 100, 2, 0, 0, {}, 12}; std::strcpy(layout.font, argv[1]);
    std::vector<uint8_t> pixels(layout.width * layout.height / 2);
    for (const char *name : {"fixture.html", "fixture.txt", "fixture.fb2", "fixture.md", "fixture.rtf", "fixture.epub", "pagination.html"}) {
        records = {}; hits = 0; auto path = std::string(argv[2]) + '/' + name;
        auto *document = open(path, layout); assert(cre_document_checkpoint(document) == CRE_OK);
        auto state = prepare(document, pixels, 0); auto first = pixels;
        assert(!hits && !state.complete); assert(cre_document_checkpoint(document) == CRE_OK);
        unsigned writes = records.writes; assert(writes); cre_document_close(document);
        document = open(path, layout); auto restored = prepare(document, pixels, 0);
        assert(hits && pixels == first && restored.pages == state.pages && restored.complete == state.complete);
        assert(records.writes == writes);
        uint32_t confirmed, complete;
        assert(cre_document_extend(document, 4, &confirmed, &complete) == CRE_OK && confirmed >= 4);
        prepare(document, pixels, 3); auto fourth = pixels;
        assert(cre_document_checkpoint(document) == CRE_OK); cre_document_close(document);
        document = open(path, layout); prepare(document, pixels, 3); assert(pixels == fourth); cre_document_close(document);
        auto baseline = records.data;
        // 中文：损坏根或延迟读取的任意块时，单次请求必须自行回退到源。
        // English: A corrupt root or lazily read block must fall back to source within the same request.
        for (unsigned fault = 0; fault < 30; ++fault) {
            records.fail_read_at = records.reads + fault;
            document = open(path, layout); prepare(document, pixels, 0); assert(pixels == first);
            cre_document_close(document); records.fail_read_at = UINT32_MAX;
        }
        for (bool after : {false, true}) for (unsigned fault = 0; fault < 10; ++fault) {
            records.data = baseline; document = open(path, layout); prepare(document, pixels, 0);
            assert(cre_document_extend(document, 6, &confirmed, &complete) == CRE_OK);
            records.fail_at = records.writes + fault; records.fail_after_write = after;
            auto result = cre_document_checkpoint(document); assert(result == CRE_OK || result == CRE_IO);
            records.fail_at = UINT32_MAX; records.fail_after_write = false;
            prepare(document, pixels, 0); assert(pixels == first);
            cre_document_close(document);
            document = open(path, layout); prepare(document, pixels, 0); assert(pixels == first); cre_document_close(document);
        }
        records.data = baseline;
        for (unsigned option = 0; option < 9; ++option) {
            auto changed = layout;
            uint32_t *options[] = {&changed.width, &changed.height, &changed.pixels, &changed.weight, &changed.margin,
                &changed.line_percent, &changed.paragraph_quarters, &changed.indent, &changed.bottom_margin};
            if (option == 0 || option == 1) *options[option] -= 2; else ++*options[option];
            unsigned before = hits; document = open(path, changed); prepare(document, pixels, 0);
            assert(hits == before); cre_document_close(document);
        }
        assert(!platform.files);
        std::printf("%s persisted, resumed, isolated; 30 read and 20 write faults recovered\n", name);
    }
    cre_mcu_shutdown(); assert(!platform.files && !platform.live);
}
