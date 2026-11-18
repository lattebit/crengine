/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：MCU 文档端口；只在串行工作任务调用，像素缓冲由宿主拥有。
 * English: MCU document port, called only on the serial worker with host-owned pixel buffers.
 */
#pragma once
#include "cre_mcu.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cre_document cre_document;
typedef struct {
    void *context; ///< 文档所属缓存 / Owning document cache
    int (*read)(void *, uint64_t, void *, size_t); ///< 精确读取已校验源记录，命中返回非零 / Read an exact verified source record; nonzero on a hit
    int (*write)(void *, uint64_t, const void *, size_t); ///< 延后写入，成功返回非零 / Deferred write; nonzero on success
} cre_source_cache;
/// 源缓存与排版无关；回调只在工作任务执行，不得抛异常。/ Layout-independent source cache; callbacks run on the worker and must not throw.
int cre_document_source_cache(cre_document *document, const cre_source_cache *cache);
/// 只在显式检查点持久化近期源；失败保留可重试脏项。/ Persist recent sources at explicit checkpoints; retain failed dirty entries for retry.
int cre_document_checkpoint(cre_document *document);
typedef struct {
    uint32_t width, height, pixels, weight; ///< 页面与字体参数 / Page and font parameters
    uint32_t margin, line_percent, paragraph_quarters, indent, override_style; ///< 排版选项 / Typography options
    char font[768]; ///< 字体文件 / Font file
    uint32_t bottom_margin; ///< 独立底部边距，零使用完整剩余高度 / Independent bottom margin; zero uses the full remaining height
} cre_layout;
typedef struct {
    uint32_t chapter; ///< 源章节 / Source chapter
    char path[256]; ///< 稳定内容路径 / Stable content path
} cre_anchor;
typedef struct {
    uint32_t format, chapters, has_cover; ///< 格式、章节及封面 / Format, chapters and cover
    uint64_t source_bytes; ///< 原文件字节 / Source bytes
    char title[256], author[256], language[48]; ///< 元数据 / Metadata
} cre_metadata;
typedef struct {
    uint32_t page, pages, complete; ///< 当前页、已分页数量及完成状态 / Current page, confirmed pages and completion
    cre_anchor anchor; ///< 当前内容位置 / Current content position
} cre_page_state;
typedef struct {
    uint32_t depth; ///< 目录层级 / Contents depth
    cre_anchor anchor; ///< 目标位置 / Destination
    char title[192]; ///< 标题 / Title
} cre_toc_entry;
typedef struct {
    int32_t x, y, width, height; ///< 页面内命中区域 / Hit area in page coordinates
    uint32_t image; ///< 非零为图片 / Nonzero for an image
    char href[768]; ///< 内部资源或链接 / Internal resource or link
} cre_hit;
/// 打开索引；正文在 prepare 时加载。/ Open an index; body loading starts at prepare.
int cre_document_open(const char *path, const cre_layout *layout, cre_document **out);
/// 关闭前必须停止全部调用；像素缓冲不由此接口释放。/ Stop all calls before close; this function does not release host pixels.
void cre_document_close(cre_document *document);
/// 读取已经解析的元数据。/ Read parsed metadata.
int cre_document_metadata(cre_document *document, cre_metadata *out);
/// 准备章节页或内容锚点，输出白色为 15 的只读 4bpp。/ Prepare a chapter page or anchor into white=15 packed 4bpp.
int cre_document_prepare(cre_document *document, const cre_anchor *anchor, uint32_t page,
                         uint8_t *pixels, size_t capacity, cre_page_state *out);
/// 推进至指定确定页数；零推进到结束，不生成或修改像素。/ Advance to a confirmed page count; zero finishes, without generating or modifying pixels.
int cre_document_extend(cre_document *document, uint32_t pages, uint32_t *confirmed, uint32_t *complete);
/// 设置排版并丢弃可重建布局；源索引仍可复用。/ Change layout and discard rebuildable layout while retaining the source index.
int cre_document_layout(cre_document *document, const cre_layout *layout);
/// 分批读取与正文加载无关的 EPUB 目录。/ Read EPUB contents independently of body loading.
int cre_document_toc(cre_document *document, uint32_t first, cre_toc_entry *out, uint32_t capacity, uint32_t *count);
/// 复制当前页面文本，截断仅发生在完整 UTF-8 字符边界。/ Copy current page text, truncating only at UTF-8 character boundaries.
int cre_document_text(cre_document *document, char *out, size_t capacity);
/// 封面使用同一灰阶输出；没有封面返回 CRE_CONTENT。/ Use the same grayscale output for a cover; missing covers return CRE_CONTENT.
int cre_document_cover(cre_document *document, uint8_t *pixels, size_t capacity);
/// 准备当前文档内图片，保持正文布局和内容锚点。/ Prepare an in-document image while retaining body layout and anchor.
int cre_document_image(cre_document *document, const char *href, uint8_t *pixels, size_t capacity);
/// 返回当前页点击位置的链接或图片路径。/ Return the link or image resource at a point on the current page.
int cre_document_hit(cre_document *document, int32_t x, int32_t y, char *href, size_t capacity, uint32_t *image);
/// 借用本次准备结果的命中区域，下一次引擎调用前必须复制。/ Borrow prepared hit areas; copy them before the next engine call.
const cre_hit *cre_document_hits(cre_document *document, size_t *count);
/// 将内部链接解析为稳定章节锚点。/ Resolve an internal link to a stable chapter anchor.
int cre_document_resolve(cre_document *document, const char *href, cre_anchor *anchor);
#ifdef __cplusplus
}
#endif
