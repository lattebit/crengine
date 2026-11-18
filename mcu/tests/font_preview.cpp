/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：真实字体后端的像素、归属、取消和逐分配故障回归。
 * English: Real font backend regression for pixels, ownership, cancellation and allocation failures.
 */
#include "cre_mcu.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "platform.h"
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    Platform p; auto c = config(p);
    std::vector<uint8_t> pixels(528 * 116 / 2, 0xaa), reference;
    assert(cre_mcu_initialize(&c) == CRE_OK);
    uint32_t missing = 0, weight_axis = 0;
    assert(cre_mcu_font_preview(argv[1], "Reading 123 ABC", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_OK);
    assert(!missing); assert(p.files == 0);
    assert(weight_axis == (argc == 3));
    bool ink = false, paper = false;
    for (auto pixel : pixels) { ink |= pixel != 0xff; paper |= pixel == 0xff; }
    assert(ink && paper); reference = pixels;
    assert(cre_mcu_font_preview(argv[1], "Reading 123 ABC", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_OK);
    assert(pixels == reference);
    // 中文：静态字形没有细体时保留原笔画；真正的 wght 轴仍能选择细体。
    // English: Keep static outlines when no light face exists; real wght axes still select lighter designs.
    assert(cre_mcu_font_preview(argv[1], "Reading 123 ABC", 32, 300, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_OK);
    if (!weight_axis) assert(pixels == reference);
    assert(cre_mcu_font_preview(argv[1], "\xf4\x8f\xbf\xbf", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_OK);
    assert(missing);
    p.cancel = true;
    assert(cre_mcu_font_preview(argv[1], "cancel", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_CANCELLED);
    p.cancel = false; p.fail_read = true;
    assert(cre_mcu_font_preview(argv[1], "read failure", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_IO);
    p.fail_read = false; assert(!p.files);
    cre_mcu_stats_t stats{}; cre_mcu_statistics(&stats); assert(stats.peak <= c.memory_limit);
    cre_mcu_shutdown(); assert(!p.live && !p.files);
    // 中文：每个分配点单独失败，必须可关闭并重新初始化。
    // English: Fail each allocation independently; shutdown and reinitialization must remain safe.
    Platform baseline; auto bc = config(baseline); assert(cre_mcu_initialize(&bc) == CRE_OK);
    assert(cre_mcu_font_preview(argv[1], "Allocation cleanup", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis) == CRE_OK);
    size_t count = baseline.allocations; cre_mcu_shutdown(); assert(!baseline.live && !baseline.files);
    std::fprintf(stderr, "allocation points=%zu\n", count);
    for (size_t fail = 0; fail < count; ++fail) {
        Platform fault; fault.fail_at = fail; auto fc = config(fault);
        int result = cre_mcu_initialize(&fc);
        if (result == CRE_OK) result = cre_mcu_font_preview(argv[1], "Allocation cleanup", 32, 400, pixels.data(), 528, 116, &missing, &weight_axis);
        (void)result; cre_mcu_shutdown();
        if (fault.live || fault.files) { std::fprintf(stderr, "failure=%zu live=%zu files=%zu\n", fail, fault.live, fault.files); return 1; }
    }
    std::printf("font pixels, missing glyphs, cancellation, I/O and %zu allocation failures passed; peak=%zu\n", count, stats.peak);
}
