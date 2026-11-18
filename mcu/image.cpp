/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：JPEG/PNG 尺寸探测及宿主灰阶解码桥接；按目标尺寸输出，不创建完整彩色副本。
 * English: JPEG/PNG probing and host grayscale decoding at target dimensions without a full color copy.
 */
#include "cre_mcu_internal.h"
#include "lvimg.h"
#include <cstring>
#include <memory>

namespace {
struct Buffer {
    void *data = nullptr;
    explicit Buffer(size_t bytes = 0) { if (bytes) { data = cre_mcu_allocate(bytes); if (!data) throw CreMcuFailure{CRE_MEMORY}; } }
    ~Buffer() { cre_mcu_release(data); }
};
class Image : public LVImageSource {
    LVStreamRef stream;
    ldomDocument *document;
    ldomNode *node;
    int width = 0, height = 0;
    bool read(uint64_t at, uint8_t *out, size_t size) {
        cre_mcu_checkpoint(); lvsize_t actual = 0;
        return at <= stream->GetSize() && size <= stream->GetSize() - at && stream->SetPos(at) == at &&
            stream->Read(out, size, &actual) == LVERR_OK && actual == size;
    }
public:
    Image(LVStreamRef source, ldomDocument *doc, ldomNode *n) : stream(source), document(doc), node(n) {}
    ldomDocument *GetSourceDocument() override { return document; }
    ldomNode *GetSourceNode() override { return node; }
    LVStream *GetSourceStream() override { return stream.get(); }
    void Compact() override {}
    int GetWidth() const override { return width; }
    int GetHeight() const override { return height; }
    bool IsScalable() const override { return true; }
    bool probe() {
        if (stream->GetSize() > 2u * 1024 * 1024) return false;
        uint8_t head[24]{}; if (!read(0, head, sizeof(head))) return false;
        if (!std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) && !std::memcmp(head + 12, "IHDR", 4)) {
            auto u32 = [](const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; };
            uint32_t w = u32(head + 16), h = u32(head + 20);
            if (!w || !h || w > 4096 || h > 4096 || uint64_t(w) * h > 1024 * 1024) return false;
            width = w; height = h; return true;
        }
        if (head[0] != 0xff || head[1] != 0xd8) return false;
        uint64_t at = 2;
        while (at < stream->GetSize()) {
            uint8_t marker[4]; if (!read(at, marker, 2) || marker[0] != 0xff) return false;
            ++at; while (marker[1] == 0xff) { if (!read(++at, marker + 1, 1)) return false; }
            ++at;
            unsigned type = marker[1]; if (!type || type == 0xda || type == 0xd9) return false;
            if (type == 1 || (type >= 0xd0 && type <= 0xd7)) continue;
            if (!read(at, marker, 2)) return false;
            unsigned size = unsigned(marker[0]) * 256 + marker[1];
            if (size < 2 || size > stream->GetSize() - at) return false;
            if (type >= 0xc0 && type <= 0xcf && type != 0xc4 && type != 0xc8 && type != 0xcc) {
                if (size < 8 || !read(at + 3, marker, 4)) return false;
                height = unsigned(marker[0]) * 256 + marker[1]; width = unsigned(marker[2]) * 256 + marker[3];
                return width && height && width <= 8192 && height <= 8192 && uint64_t(width) * height <= 16u * 1024 * 1024;
            }
            at += size;
        }
        return false;
    }
    bool Decode(LVImageDecoderCallback *callback) override {
        if (!callback) return width && height;
        int w = width, h = height; (void)callback->GetTargetSize(w, h);
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
        Buffer input(stream->GetSize());
        if (!read(0, static_cast<uint8_t *>(input.data), stream->GetSize())) throw CreMcuFailure{CRE_IO};
        cre_mcu_bitmap bitmap{};
        int rc = cre_mcu_decode_image(static_cast<const uint8_t *>(input.data), stream->GetSize(), w, h, &bitmap);
        Buffer decoded; decoded.data = bitmap.pixels;
        cre_mcu_release(input.data); input.data = nullptr;
        if (rc == CRE_MEMORY || rc == CRE_CANCELLED || rc == CRE_IO) throw CreMcuFailure{rc};
        if (rc != CRE_OK || !bitmap.pixels || !bitmap.width || !bitmap.height || bitmap.width > uint32_t(w) || bitmap.height > uint32_t(h)) return false;
        Buffer row(static_cast<size_t>(w) * sizeof(lUInt32)); auto *line = static_cast<lUInt32 *>(row.data);
        callback->OnStartDecode(this);
        try {
            for (int y = 0; y < h; ++y) {
                cre_mcu_checkpoint(); const auto *source = bitmap.pixels + (uint64_t(y) * bitmap.height / h) * bitmap.width;
                for (int x = 0; x < w; ++x) line[x] = source[uint64_t(x) * bitmap.width / w] * 0x010101u;
                if (!callback->OnLineDecoded(this, y, line)) { callback->OnEndDecode(this, true); return false; }
            }
        } catch (...) { callback->OnEndDecode(this, true); throw; }
        callback->OnEndDecode(this, false); return true;
    }
};
}
LVImageSource *cre_mcu_image_source(LVStreamRef stream, ldomDocument *doc, ldomNode *node) {
    auto image = std::make_unique<Image>(stream, doc, node);
    if (!image->probe()) return nullptr;
    return image.release();
}
