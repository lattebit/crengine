/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：串行 MCU 引擎端口；平台提供受管文件、内存和取消，不暴露 C++ 对象。
 * English: Serial MCU engine port; the platform owns managed I/O, memory and cancellation without exposing C++ objects.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
    CRE_OK = 0, ///< 成功 / Success
    CRE_INVALID = -1, ///< 输入无效 / Invalid input
    CRE_MEMORY = -2, ///< 内存不足 / Out of memory
    CRE_IO = -3, ///< 文件错误 / File failure
    CRE_CANCELLED = -4, ///< 请求取消 / Cancelled request
    CRE_CONTENT = -5, ///< 不支持或损坏内容 / Unsupported or invalid content
    CRE_BUSY = -6 ///< 资源仍在使用 / Resources remain in use
};
typedef struct {
    void *(*allocate)(size_t); ///< 同一引擎预算 / Shared engine allocation budget
    void *(*resize)(void *, size_t); ///< 失败保留旧块 / Preserve the old allocation on failure
    void (*release)(void *); ///< 配对释放 / Matching release
    void (*yield)(void); ///< 协作让行，不抛异常 / Cooperative yield without exceptions
    int (*cancelled)(void); ///< 取消检查，不抛异常 / Cancellation check without exceptions
} cre_mcu_image_memory;
typedef struct {
    uint8_t *pixels; ///< 分配器拥有的连续八位灰阶 / Allocator-owned contiguous grayscale bytes
    uint32_t width, height; ///< 实际解码尺寸 / Actual decoded dimensions
} cre_mcu_bitmap;
typedef struct {
    void *context; ///< 平台上下文 / Platform context
    void *(*allocate)(void *, size_t); ///< PSRAM 优先，返回自然对齐地址 / Prefer PSRAM and return naturally aligned storage
    void (*release)(void *, void *); ///< 与 allocate 配对 / Pair with allocate
    void *(*open)(void *, const char *, uint64_t *); ///< 只读文件及长度 / Read-only file and length
    size_t (*read_at)(void *, void *, uint64_t, void *, size_t); ///< 精确定位读取，短读报告错误或 EOF / Positioned read; short counts indicate failure or EOF
    void (*close)(void *, void *); ///< 归还文件，不抛异常 / Return a file without throwing
    int (*cancelled)(void *); ///< 工作任务安全点，非零取消 / Worker checkpoint; nonzero cancels
    void (*yield)(void *); ///< 可选协作让行 / Optional cooperative yield
    size_t memory_limit; ///< 引擎可跟踪分配的硬预算 / Hard budget for tracked engine allocations
    int (*decode_image)(void *, const uint8_t *, size_t, uint32_t, uint32_t,
                        const cre_mcu_image_memory *, cre_mcu_bitmap *); ///< 可选宿主 JPEG/PNG 解码，必须使用给定分配器 / Optional host JPEG/PNG decoding using the supplied allocator
} cre_mcu_platform_t;
typedef struct {
    size_t used, peak, limit; ///< 当前、峰值和预算字节 / Current, peak and budget bytes
    uint32_t open_files; ///< 当前共享字体源数量 / Open shared font sources
    uint64_t read_calls, read_bytes; ///< 实际字体文件读取计数 / Physical font reads and bytes
} cre_mcu_stats_t;
/// 仅工作任务初始化；所有请求串行。/ Initialize on the sole worker; serialize all requests.
int cre_mcu_initialize(const cre_mcu_platform_t *platform);
/// 无副作用的构建身份；可在缓存命中路径查询。/ Side-effect-free build identity, safe on cache-hit paths.
uint64_t cre_mcu_build_identity(void);
/// 无文档或借用像素后关闭字体后端。/ Shut down fonts after documents and pixel loans end.
void cre_mcu_shutdown(void);
/// 查询同一工作任务的预算统计。/ Query budget statistics on the same worker.
void cre_mcu_statistics(cre_mcu_stats_t *out);
/// 直接使用字体后端生成一行 4bpp，白色 15、高半字节在左；不创建文档。
/// Render one line through the font backend, 4bpp white=15 with left pixels in high nibbles; no document is created.
int cre_mcu_font_preview(const char *path, const char *text, uint32_t pixels, uint32_t weight,
                         uint8_t *output, uint32_t width, uint32_t height, uint32_t *missing);
#ifdef __cplusplus
}
#endif
