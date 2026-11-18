/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：JPEG/PNG 尺寸探测及宿主灰阶解码桥接；按目标尺寸输出，不创建完整彩色副本。
 * English: JPEG/PNG probing and host grayscale decoding at target dimensions without a full color copy.
 */
#include "cre_mcu_internal.h"
#include "lvimg.h"
#include "lvtinydom.h"
#include "fb2def.h"
#include <cstdio>
#include <cmath>
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

// 中文：全画布单张图片的 SVG 包装复用灰阶解码；复杂图形继续交给矢量后端。
// English: Full-canvas single-image SVG wrappers reuse grayscale decoding; complex graphics retain the vector backend.
ldomNode *cre_mcu_svg_image_node(ldomNode *node) {
    if (!node || node->getNodeId() != el_svg) return nullptr;
    ldomNode *image = nullptr;
    for (int i = 0; i < node->getChildCount(); ++i) {
        auto *child = node->getChildNode(i);
        if (child->isText() && child->isWhitespaceText()) continue;
        if (!child->isElement() || child->getNodeId() != el_image || image) return nullptr;
        image = child;
    }
    if (!image) return nullptr;
    auto aspect = image->getAttributeValue("preserveAspectRatio");
    if (!aspect.empty() && aspect != U"xMidYMid meet") return nullptr;
    for (auto *part : {node, image}) for (const char *attribute : {"transform", "clip-path", "mask", "opacity", "style"})
        if (!part->getAttributeValue(attribute).empty()) return nullptr;
    for (const char *attribute : {"x", "y"}) {
        auto value = image->getAttributeValue(attribute);
        if (!value.empty() && value != U"0" && value != U"0px") return nullptr;
    }
    auto viewbox = UnicodeToUtf8(node->getAttributeValue("viewBox"));
    double x, y, w, h; char extra;
    if (std::sscanf(viewbox.c_str(), "%lf %lf %lf %lf %c", &x, &y, &w, &h, &extra) != 4 || x != 0 || y != 0 ||
        !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) return nullptr;
    for (const char *attribute : {"width", "height"}) {
        auto value = UnicodeToUtf8(image->getAttributeValue(attribute)); double size;
        if (std::sscanf(value.c_str(), "%lf %c", &size, &extra) != 1 || size != (attribute[0] == 'w' ? w : h)) return nullptr;
    }
    return image;
}

namespace {
// 中文：保留 SVG 视口的固有尺寸，实际目标像素仍由共享图片解码器生成。
// English: Preserve intrinsic SVG viewport dimensions while the shared decoder produces target pixels.
class SvgImage : public LVImageSource {
    LVImageSourceRef source;
    ldomNode *node;
    int width, height;
public:
    SvgImage(LVImageSourceRef image, ldomNode *n, int w, int h) : source(image), node(n), width(w), height(h) {}
    ldomDocument *GetSourceDocument() override { return node->getDocument(); }
    ldomNode *GetSourceNode() override { return node; }
    LVStream *GetSourceStream() override { return source->GetSourceStream(); }
    void Compact() override { source->Compact(); }
    int GetWidth() const override { return width; }
    int GetHeight() const override { return height; }
    bool IsScalable() const override { return true; }
    bool Decode(LVImageDecoderCallback *callback) override { return source->Decode(callback); }
};
}
LVImageSource *cre_mcu_svg_image_source(ldomNode *node) {
    auto *child = cre_mcu_svg_image_node(node); if (!child) return nullptr;
    auto image = child->getObjectImageSource(); if (image.isNull()) return nullptr;
    auto viewbox = UnicodeToUtf8(node->getAttributeValue("viewBox")); double x, y, w, h;
    if (std::sscanf(viewbox.c_str(), "%lf %lf %lf %lf", &x, &y, &w, &h) != 4) return nullptr;
    double width = w, height = h;
    for (const char *attribute : {"width", "height"}) {
        auto value = UnicodeToUtf8(node->getAttributeValue(attribute));
        if (value.empty() || value.endsWith("%")) continue;
        double length; char unit[3] = {}, extra;
        int count = std::sscanf(value.c_str(), "%lf%2s %c", &length, unit, &extra);
        if ((count != 1 && (count != 2 || std::strcmp(unit, "px"))) || !std::isfinite(length) || length < 1 || length > 1000000) return nullptr;
        (attribute[0] == 'w' ? width : height) = length;
    }
    if (width < 1 || height < 1 || width > 1000000 || height > 1000000 ||
        std::abs(width * h - height * w) > 0.001 || image->GetWidth() <= 0 || image->GetHeight() <= 0 ||
        std::abs(double(image->GetWidth()) * h - double(image->GetHeight()) * w) > 0.001) return nullptr;
    return new SvgImage(image, node, int(width), int(height));
}
