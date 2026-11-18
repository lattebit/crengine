/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：嵌入字体的有界读取、跨字号共享、像素等价及故障清理。
 * English: Bounded embedded-font reads, sharing across sizes, pixel parity and failure cleanup.
 */
#include "platform.h"
#include "cre_mcu_internal.h"
#include "lvfntman.h"
#include "lvdrawbuf.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <unistd.h>

static char cache_path[] = "/tmp/crengine-font-XXXXXX";
static unsigned extractions = 0;
static int extract(void *, const char *name, uint64_t bytes, cre_mcu_resource_read read, void *context, char *out, size_t capacity) {
    assert(!std::strcmp(name, "font.ttf") && capacity > sizeof(cache_path));
    if (!extractions) {
        FILE *f = std::fopen(cache_path, "wb"); assert(f); uint8_t block[4096];
        for (uint64_t offset = 0; offset < bytes;) {
            size_t count = std::min<uint64_t>(sizeof(block), bytes - offset);
            int rc = read(context, offset, block, count);
            if (rc != CRE_OK) { std::fclose(f); return rc; }
            assert(std::fwrite(block, 1, count, f) == count); offset += count;
        }
        assert(!std::fclose(f)); ++extractions;
    }
    std::strcpy(out, cache_path); return CRE_OK;
}

struct Input {
    std::vector<uint8_t> bytes;
    size_t reads = 0, largest = 0, live = 0;
    bool fail = false;
};
class BoundedStream : public LVNamedStream {
    Input &input;
    lvsize_t position = 0;
    static constexpr lvsize_t length = 8u * 1024 * 1024;
public:
    explicit BoundedStream(Input &in) : input(in) { ++input.live; m_mode = LVOM_READ; }
    ~BoundedStream() override { --input.live; }
    lvsize_t GetSize() override { return length; }
    lverror_t GetSize(lvsize_t *out) override { *out = length; return LVERR_OK; }
    lvpos_t GetPos() override { return position; }
    lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t *out) override {
        int64_t next = offset + (origin == LVSEEK_SET ? 0 : origin == LVSEEK_CUR ? position : length);
        if (next < 0 || next > length) return LVERR_FAIL;
        position = next; if (out) *out = position; return LVERR_OK;
    }
    lverror_t Read(void *out, lvsize_t bytes, lvsize_t *actual) override {
        ++input.reads; input.largest = std::max<size_t>(input.largest, bytes);
        if (actual) *actual = 0;
        if (input.fail) throw CreMcuFailure{CRE_IO};
        if (bytes > 4096) return LVERR_FAIL;
        bytes = std::min(bytes, length - position); std::memset(out, 0, bytes);
        if (position < input.bytes.size()) std::memcpy(out, input.bytes.data() + position, std::min<size_t>(bytes, input.bytes.size() - position));
        position += bytes; if (actual) *actual = bytes; return LVERR_OK;
    }
    lverror_t SetSize(lvsize_t) override { return LVERR_FAIL; }
    lverror_t Write(const void *, lvsize_t, lvsize_t *) override { return LVERR_FAIL; }
    bool Eof() override { return position == length; }
};
class Container : public LVNamedContainer {
    Input &input;
public:
    explicit Container(Input &in) : input(in) {}
    LVContainer *GetParentContainer() override { return nullptr; }
    lverror_t GetSize(lvsize_t *out) override { *out = 0; return LVERR_OK; }
    LVStreamRef OpenStream(const lChar32 *, lvopen_mode_t mode) override {
        assert(mode == LVOM_READ); return LVStreamRef(new BoundedStream(input));
    }
};
static std::vector<uint8_t> draw(LVFontRef font) {
    assert(!font.isNull());
    std::vector<uint8_t> pixels(480 * 96, 255);
    LVGrayDrawBuf buf(480, 96, 8, pixels.data()); buf.SetTextColor(0); buf.SetBackgroundColor(0xffffff);
    lString32 text(U"Reading 123 ABC");
    font->DrawTextString(&buf, 0, 0, text.c_str(), text.length(), U'?', nullptr, false, 0);
    assert(std::find_if(pixels.begin(), pixels.end(), [](uint8_t p) { return p != 255; }) != pixels.end());
    return pixels;
}
int main(int argc, char **argv) {
    assert(argc == 2); Input input;
    FILE *file = std::fopen(argv[1], "rb"); assert(file); assert(!std::fseek(file, 0, SEEK_END));
    input.bytes.resize(std::ftell(file)); std::rewind(file);
    assert(std::fread(input.bytes.data(), 1, input.bytes.size(), file) == input.bytes.size()); std::fclose(file);
    int temporary = mkstemp(cache_path); assert(temporary >= 0); close(temporary);
    for (bool disk : {false, true}) {
        Platform p; auto cfg = config(p); cfg.cache_font = disk ? extract : nullptr; assert(cre_mcu_initialize(&cfg) == CRE_OK);
        LVContainerRef container(new Container(input));
        assert(fontMan->RegisterDocumentFont(77, container, lString32(U"font.ttf"), lString8("embedded"), 400, 100, false, 0));
        size_t reads = input.reads;
        assert(fontMan->RegisterDocumentFont(77, container, lString32(U"font.ttf"), lString8("embedded"), 400, 100, false, 1));
        assert(input.reads == reads && input.live == (disk ? 0u : 1u));
        assert(fontMan->RegisterExternalFont(78, Utf8ToUnicode(argv[1]), lString8("external"), 400, 100, false));
        for (unsigned size : {18u, 32u, 48u}) {
            auto embedded = fontMan->GetFont(size, 400, false, css_ff_sans_serif, lString8("embedded"), 0, 77, false, nullptr, size == 18 ? 0 : 1);
            auto external = fontMan->GetFont(size, 400, false, css_ff_sans_serif, lString8("external"), 0, 78);
            assert(draw(embedded) == draw(external));
            assert(input.live == (disk ? 0u : 1u) && input.largest <= 4096);
        }
        fontMan->UnregisterDocumentFonts(77); fontMan->UnregisterDocumentFonts(78); fontMan->gc();
        assert(!input.live && !p.files);
        if (disk) {
            reads = input.reads;
            assert(fontMan->RegisterDocumentFont(79, container, lString32(U"font.ttf"), lString8("embedded"), 400, 100, false));
            assert(input.reads == reads && extractions == 1);
            fontMan->UnregisterDocumentFonts(79); fontMan->gc();
        }
        cre_mcu_stats_t stats{}; cre_mcu_statistics(&stats); assert(stats.peak < cfg.memory_limit);
        cre_mcu_shutdown(); assert(!p.live && !p.files && !input.live);
    }
    assert(!unlink(cache_path));
    size_t points = 0;
    // 中文：缓存建立和 FreeType 的每个分配点失败，旧引用和文件必须仍可归还。
    // English: Fail cache construction and every FreeType allocation; existing references and files remain releasable.
    for (size_t iteration = 0; iteration <= points; ++iteration) {
        Platform fault; auto fc = config(fault); assert(cre_mcu_initialize(&fc) == CRE_OK);
        size_t start = fault.allocations;
        if (iteration) fault.fail_at = start + iteration - 1;
        try {
            LVStreamRef source = cre_mcu_font_stream(LVStreamRef(new BoundedStream(input)));
            FT_Face face = nullptr;
            FT_Error rc = cre_mcu_new_stream_face(cre_mcu_font_library(), source, 0, &face);
            if (!rc) {
                assert(face);
                rc = FT_Set_Pixel_Sizes(face, 0, 32);
                if (!rc) rc = FT_Load_Char(face, 'R', FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT);
                FT_Done_Face(face);
            }
            if (!iteration) { assert(!rc); points = fault.allocations - start; }
        } catch (const CreMcuFailure &failure) { assert(iteration && failure.code == CRE_MEMORY); }
        cre_mcu_shutdown(); assert(!fault.live && !fault.files && !input.live);
    }
    // 中文：取消和源异常不得越过 FreeType C 回调；外层能取得准确错误。
    // English: Cancellation and source exceptions stay inside FreeType callbacks and retain the outer error code.
    for (bool cancel : {false, true}) {
        Platform fault; auto fc = config(fault); assert(cre_mcu_initialize(&fc) == CRE_OK);
        {
            LVStreamRef source = cre_mcu_font_stream(LVStreamRef(new BoundedStream(input)));
            fault.cancel = cancel; input.fail = !cancel; FT_Face face = nullptr;
            assert(cre_mcu_new_stream_face(cre_mcu_font_library(), source, 0, &face) && !face);
            assert(cre_mcu_error() == (cancel ? CRE_CANCELLED : CRE_IO));
            fault.cancel = false; input.fail = false;
        }
        cre_mcu_shutdown(); assert(!fault.live && !fault.files && !input.live);
    }
    std::printf("embedded fonts: three sizes, shared bounded reads, pixel parity and %zu allocation failures passed\n", points);
}
