/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：MCU 字体共享流和预算；引用归零即关闭，预览不会改变阅读或 UI 字体。
 * English: Budgeted shared font streams; close on the final reference and isolate preview font selection.
 */
#include "cre_mcu_internal.h"
#include "cre_identity.h"
#include "lvfntman.h"
#include "lvdrawbuf.h"
#include FT_MODULE_H
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <climits>
#include <new>

namespace {
cre_mcu_platform_t platform{};
cre_mcu_stats_t stats{};
bool initialized = false;
int io_error = CRE_OK;
FT_Library shared_library = nullptr;
union Header {
    struct { size_t bytes; void (*release)(void *, void *); void *context; } block;
    max_align_t align;
};
constexpr size_t block_bytes = 4096, blocks = 4, source_limit = 8;
struct Source {
    Source *next = nullptr;
    void *file = nullptr;
    uint64_t size = 0, clock = 0;
    uint32_t references = 0;
    char path[768]{};
    struct Block { uint64_t offset = UINT64_MAX, used = 0; size_t bytes = 0; uint8_t data[block_bytes]; } cache[blocks];
};
Source *sources = nullptr;
struct FaceStream { FT_StreamRec stream{}; Source *source = nullptr; };
void releaseSource(Source *source) {
    if (--source->references) return;
    Source **link = &sources;
    while (*link != source) link = &(*link)->next;
    *link = source->next;
    platform.close(platform.context, source->file);
    --stats.open_files;
    source->~Source(); cre_mcu_release(source);
}
void closeFace(FT_Stream stream) {
    auto *face = reinterpret_cast<FaceStream *>(stream);
    if (face->source) releaseSource(face->source);
    face->~FaceStream(); cre_mcu_release(face);
}
unsigned long readFace(FT_Stream stream, unsigned long offset, unsigned char *out, unsigned long bytes) {
    auto *source = reinterpret_cast<FaceStream *>(stream)->source;
    if (!bytes) return offset <= source->size ? 0 : 1;
    size_t done = 0;
    if (offset > source->size || bytes > source->size - offset) { io_error = CRE_IO; return 0; }
    while (done < bytes) {
        if (platform.cancelled && platform.cancelled(platform.context)) { io_error = CRE_CANCELLED; break; }
        uint64_t at = offset + done, base = at / block_bytes * block_bytes;
        Source::Block *block = nullptr, *victim = &source->cache[0];
        for (auto &item : source->cache) {
            if (item.offset == base) block = &item;
            if (item.used < victim->used) victim = &item;
        }
        if (!block) {
            block = victim; block->offset = UINT64_MAX;
            size_t count = static_cast<size_t>(std::min<uint64_t>(block_bytes, source->size - base));
            block->bytes = platform.read_at(platform.context, source->file, base, block->data, count);
            ++stats.read_calls; stats.read_bytes += block->bytes;
            if (block->bytes != count) { io_error = CRE_IO; break; }
            block->offset = base;
        }
        block->used = ++source->clock;
        size_t start = static_cast<size_t>(at - base);
        size_t count = std::min<size_t>(bytes - done, block->bytes - start);
        std::memcpy(out + done, block->data + start, count); done += count;
    }
    return static_cast<unsigned long>(done);
}
void *ftAllocate(FT_Memory, long bytes) { return bytes > 0 ? cre_mcu_allocate(static_cast<size_t>(bytes)) : nullptr; }
void ftRelease(FT_Memory, void *data) { cre_mcu_release(data); }
void *ftResize(FT_Memory, long old_size, long new_size, void *old) {
    if (new_size <= 0) { cre_mcu_release(old); return nullptr; }
    void *next = cre_mcu_allocate(static_cast<size_t>(new_size));
    if (!next) return nullptr;
    if (old) std::memcpy(next, old, static_cast<size_t>(std::min(old_size, new_size)));
    cre_mcu_release(old); return next;
}
FT_MemoryRec_ ftMemory{nullptr, ftAllocate, ftRelease, ftResize};
struct TemporaryFont {
    static constexpr int id = 0x70000000;
    LVFontRef font;
    ~TemporaryFont() { font.Clear(); fontMan->UnregisterDocumentFonts(id); fontMan->gc(); }
};

// 中文：文件本身由宿主登记，所有定位读取都经过平台的 Flash 栈转发。
// English: The host registers the file; every positioned read uses platform Flash-stack forwarding.
class ManagedStream : public LVNamedStream {
    void *file;
    lvsize_t length, position = 0;
public:
    ManagedStream(void *input, lvsize_t bytes, const lChar32 *path) : file(input), length(bytes) {
        m_mode = LVOM_READ; SetName(path);
    }
    ~ManagedStream() override { platform.close(platform.context, file); }
    lverror_t GetSize(lvsize_t *out) override { if (out) *out = length; return LVERR_OK; }
    lvsize_t GetSize() override { return length; }
    lvpos_t GetPos() override { return position; }
    lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t *out) override {
        int64_t base = origin == LVSEEK_SET ? 0 : origin == LVSEEK_CUR ? position : origin == LVSEEK_END ? length : -1;
        int64_t next = base + offset;
        if (base < 0 || next < 0 || next > length) return LVERR_FAIL;
        position = static_cast<lvpos_t>(next); if (out) *out = position; return LVERR_OK;
    }
    lverror_t SetSize(lvsize_t) override { return LVERR_FAIL; }
    lverror_t Write(const void *, lvsize_t, lvsize_t *out) override { if (out) *out = 0; return LVERR_FAIL; }
    lverror_t Read(void *out, lvsize_t bytes, lvsize_t *actual) override {
        if (actual) *actual = 0;
        cre_mcu_checkpoint();
        lvsize_t count = std::min(bytes, length - position);
        if (!count) return LVERR_OK;
        size_t read = platform.read_at(platform.context, file, position, out, count);
        if (read > count) { io_error = CRE_IO; throw CreMcuFailure{CRE_IO}; }
        position += read; if (actual) *actual = read;
        if (read != count) { io_error = CRE_IO; throw CreMcuFailure{CRE_IO}; }
        return LVERR_OK;
    }
    bool Eof() override { return position == length; }
};
class ManagedDirectory : public LVNamedContainer {
public:
    explicit ManagedDirectory(const lChar32 *path) { m_path_separator = '/'; SetName(path); }
    LVContainer *GetParentContainer() override { return nullptr; }
    lverror_t GetSize(lvsize_t *out) override { if (out) *out = 0; return LVERR_OK; }
    LVStreamRef OpenStream(const lChar32 *name, lvopen_mode_t mode) override {
        return cre_mcu_open_stream(LVCombinePaths(m_fname, lString32(name)).c_str(), mode);
    }
};
}
void *cre_mcu_allocate(size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(Header) || bytes + sizeof(Header) > stats.limit - std::min(stats.limit, stats.used)) { io_error = CRE_MEMORY; return nullptr; }
    auto *h = static_cast<Header *>(platform.allocate(platform.context, bytes + sizeof(Header)));
    if (!h) { io_error = CRE_MEMORY; return nullptr; }
    h->block = {bytes + sizeof(Header), platform.release, platform.context};
    stats.used += h->block.bytes; stats.peak = std::max(stats.peak, stats.used);
    return h + 1;
}
void cre_mcu_release(void *data) {
    if (!data) return;
    auto *h = static_cast<Header *>(data) - 1;
    stats.used -= h->block.bytes;
    h->block.release(h->block.context, h);
}
void cre_mcu_checkpoint() {
    if (platform.cancelled && platform.cancelled(platform.context)) throw CreMcuFailure{CRE_CANCELLED};
    if (platform.yield) platform.yield(platform.context);
}
void cre_mcu_reset_error() { io_error = CRE_OK; }
int cre_mcu_error() { return io_error; }
FT_Error cre_mcu_new_library(FT_Library *out) {
    FT_Error error = FT_New_Library(&ftMemory, out);
    if (!error) { FT_Add_Default_Modules(*out); shared_library = *out; }
    return error;
}
FT_Library cre_mcu_font_library() { return shared_library; }
LVStreamRef cre_mcu_open_stream(const lChar32 *path, int mode) {
    if (!platform.open || mode != LVOM_READ || !path) return LVStreamRef();
    cre_mcu_checkpoint();
    uint64_t bytes = 0;
    void *file = platform.open(platform.context, UnicodeToUtf8(lString32(path)).c_str(), &bytes);
    if (!file) return LVStreamRef();
    if (bytes >= INT32_MAX) { platform.close(platform.context, file); return LVStreamRef(); }
    try { return LVStreamRef(new ManagedStream(file, static_cast<lvsize_t>(bytes), path)); }
    catch (...) { platform.close(platform.context, file); throw; }
}
LVContainerRef cre_mcu_open_directory(const lChar32 *path) {
    return path ? LVContainerRef(new ManagedDirectory(path)) : LVContainerRef();
}
FT_Error cre_mcu_new_face(FT_Library library, const char *path, FT_Long index, FT_Face *out) {
    *out = nullptr;
    if (!path || std::strlen(path) >= sizeof(Source::path)) return FT_Err_Invalid_Argument;
    Source *source = sources;
    while (source && std::strcmp(source->path, path)) source = source->next;
    if (!source) {
        if (stats.open_files >= source_limit) return FT_Err_Out_Of_Memory;
        void *storage = cre_mcu_allocate(sizeof(Source));
        if (!storage) return FT_Err_Out_Of_Memory;
        source = new (storage) Source;
        source->file = platform.open(platform.context, path, &source->size);
        if (!source->file || source->size > ULONG_MAX) {
            if (source->file) platform.close(platform.context, source->file);
            source->~Source(); cre_mcu_release(source); io_error = CRE_IO;
            return FT_Err_Cannot_Open_Resource;
        }
        std::strcpy(source->path, path);
        source->next = sources; sources = source; ++stats.open_files;
    }
    ++source->references;
    void *storage = cre_mcu_allocate(sizeof(FaceStream));
    if (!storage) { releaseSource(source); return FT_Err_Out_Of_Memory; }
    auto *face = new (storage) FaceStream;
    face->source = source; face->stream.size = static_cast<unsigned long>(source->size);
    face->stream.read = readFace; face->stream.close = closeFace;
    FT_Open_Args args{}; args.flags = FT_OPEN_STREAM; args.stream = &face->stream;
    // 中文：FT_Open_Face 的失败路径也调用 stream.close，避免重复释放。
    // English: FT_Open_Face also closes the supplied stream on failure; never release it twice.
    return FT_Open_Face(library, &args, index, out);
}
extern "C" int cre_mcu_initialize(const cre_mcu_platform_t *p) {
    if (!p || !p->allocate || !p->release || !p->open || !p->read_at || !p->close || p->memory_limit < 65536) return CRE_INVALID;
    if (initialized) return CRE_BUSY;
    platform = *p; stats = {}; stats.limit = p->memory_limit; io_error = CRE_OK;
    try {
        initialized = InitFontManager(lString8());
        if (!initialized || io_error != CRE_OK) { cre_mcu_shutdown(); return CRE_MEMORY; }
        fontMan->SetKerningMode(KERNING_MODE_HARFBUZZ);
        fontMan->SetHintingMode(HINTING_MODE_DISABLED);
        return CRE_OK;
    } catch (const std::bad_alloc &) { cre_mcu_shutdown(); return CRE_MEMORY; }
      catch (...) { cre_mcu_shutdown(); return CRE_CONTENT; }
}
extern "C" uint64_t cre_mcu_build_identity(void) { return CRE_BUILD_ID; }
extern "C" void cre_mcu_shutdown(void) {
    ShutdownFontManager(); initialized = false; shared_library = nullptr;
}
extern "C" void cre_mcu_statistics(cre_mcu_stats_t *out) { if (out) *out = stats; }
extern "C" int cre_mcu_font_preview(const char *path, const char *text, uint32_t pixels, uint32_t weight,
                                    uint8_t *output, uint32_t width, uint32_t height, uint32_t *missing) {
    if (missing) *missing = 0;
    if (!initialized || !path || !text || !output || !missing || !width || width > 2048 || (width & 1)
        || !height || height > 256 || pixels < 8 || pixels > 128 || weight < 100 || weight > 900 || std::strlen(text) > 2048) return CRE_INVALID;
    io_error = CRE_OK;
    try {
        cre_mcu_checkpoint(); TemporaryFont temporary;
        if (!fontMan->RegisterExternalFont(TemporaryFont::id, Utf8ToUnicode(path), lString8("mcu-preview"), 400, 100, false))
            return io_error == CRE_OK ? CRE_CONTENT : io_error;
        temporary.font = fontMan->GetFont(pixels, weight, false, css_ff_sans_serif, lString8("mcu-preview"), 0, TemporaryFont::id);
        if (temporary.font.isNull()) return CRE_MEMORY;
        lString32 string = Utf8ToUnicode(text);
        for (int i = 0; i < string.length(); ++i) {
            cre_mcu_checkpoint(); LVFont::glyph_info_t info{};
            if (!temporary.font->getGlyphInfo(string[i], &info, 0)) *missing = 1;
        }
        // 中文：上游四位灰度仍占一字节；只在工作区转换为 SDK 的紧凑 4bpp。
        // English: Upstream four-bit gray still occupies a byte; pack SDK 4bpp only in worker storage.
        struct Pixels {
            uint8_t *data;
            ~Pixels() { cre_mcu_release(data); }
        } gray{static_cast<uint8_t *>(cre_mcu_allocate(static_cast<size_t>(width) * height))};
        if (!gray.data) return CRE_MEMORY;
        LVGrayDrawBuf buffer(width, height, 8, gray.data);
        buffer.Clear(0xffffff); buffer.SetTextColor(0); buffer.SetBackgroundColor(0xffffff);
        temporary.font->DrawTextString(&buffer, 0, 0, string.c_str(), string.length(), U'?', nullptr, false);
        cre_mcu_checkpoint();
        if (io_error != CRE_OK) return io_error;
        for (size_t i = 0; i < static_cast<size_t>(width) * height; i += 2)
            output[i / 2] = (gray.data[i] & 0xf0) | (gray.data[i + 1] >> 4);
        return CRE_OK;
    } catch (const CreMcuFailure &e) { return e.code; }
      catch (const std::bad_alloc &) { return CRE_MEMORY; }
      catch (...) { return CRE_CONTENT; }
}
