/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：验证宿主解码桥接的尺寸、灰阶、目标缩放及故障清理；像素由确定性测试解码器生成。
 * English: Exercise host decoder dimensions, grayscale, target scaling and failure cleanup with a deterministic test decoder.
 */
#include "platform.h"
#include "cre_mcu_internal.h"
#include "lvimg.h"
#include "lvdrawbuf.h"
#include <cstring>
#include <initializer_list>
#include <vector>
#include <string>
#include "../deps/nanosvg/src/nanosvg.h"
#include "../deps/nanosvg/src/nanosvgrast.h"
static unsigned calls;
static uint32_t target_width,target_height;
static int decode(void *,const uint8_t *,size_t,uint32_t width,uint32_t height,const cre_mcu_image_memory *memory,cre_mcu_bitmap *out){
    ++calls;target_width=width;target_height=height;
    out->pixels=static_cast<uint8_t *>(memory->allocate(size_t(width)*height));if(!out->pixels)return CRE_MEMORY;
    out->width=width;out->height=height;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)out->pixels[y*width+x]=x<width/2?0:255;
    return CRE_OK;
}
// 中文：比较分条带光栅化与小尺寸完整光栅化，包含透明度、曲线与跨条带边缘。
// English: Compare banded rasterization with a small full reference, including alpha, curves and band crossings.
struct SvgPixels : LVImageDecoderCallback {
    std::vector<lUInt32> pixels = std::vector<lUInt32>(64 * 32);
    bool ended = false, error = false;
    bool GetTargetSize(int &w, int &h) const override { w = 64; h = 32; return true; }
    void OnStartDecode(LVImageSource *) override {}
    bool OnLineDecoded(LVImageSource *, int y, lUInt32 *row) override {
        assert(y >= 0 && y < 32); std::memcpy(pixels.data() + y * 64, row, 64 * sizeof(*row)); return true;
    }
    void OnEndDecode(LVImageSource *, bool failed) override { ended = true; error = failed; }
};
static void svg(Platform &platform) {
    std::string xml = "<svg xmlns='http://www.w3.org/2000/svg' width='126' height='62'>"
        "<rect x='0' y='0' width='126' height='62' fill='#ffffff'/>"
        "<path d='M 3 3 L 108 9 L 90 58 L 13 51 Z' fill='#006080' opacity='0.5'/>"
        "<circle cx='49' cy='29' r='19' fill='#222222'/></svg>";
    auto stream = LVCreateMemoryStream(xml.data(), xml.size(), true, LVOM_READ);
    auto source = LVCreateStreamImageSource(stream); assert(!source.isNull());
    assert(source->IsScalable() && source->GetWidth() == 128 && source->GetHeight() == 64);
    SvgPixels actual; assert(source->Decode(&actual) && actual.ended && !actual.error);
    auto *reference = nsvgParse(xml.data(), "px", 96.0f); assert(reference);
    auto *raster = nsvgCreateRasterizer(); assert(raster);
    std::vector<unsigned char> rgba(64 * 32 * 4);
    nsvgRasterize(raster, reference, 0.5f, 0.5f, 0.5f, rgba.data(), 64, 32, 64 * 4);
    for (unsigned i = 0; i < 64 * 32; ++i) {
        auto *p = rgba.data() + i * 4;
        auto expected = (lUInt32(255 - p[3]) << 24) | (lUInt32(p[0]) << 16) | (lUInt32(p[1]) << 8) | p[2];
        // 中文：条带会重启定点边缘累加，舍入误差必须小于半个 16 灰阶步长。
        // English: Bands restart fixed-point edge accumulation; rounding must stay below half a 16-gray step.
        for (unsigned shift : {0u, 8u, 16u, 24u})
            assert(std::abs(int((actual.pixels[i] >> shift) & 255) - int((expected >> shift) & 255)) < 8);
    }
    nsvgDeleteRasterizer(raster); nsvgDelete(reference);
    for (unsigned offset = 0; offset < 3; ++offset) {
        size_t live = platform.live; platform.fail_at = platform.allocations + offset; bool failed = false;
        try { SvgPixels out; source->Decode(&out); } catch (const CreMcuFailure &failure) { failed = failure.code == CRE_MEMORY; }
        assert(failed && platform.live == live); platform.fail_at = SIZE_MAX;
    }
    platform.cancel_at = platform.checkpoints + 2; SvgPixels interrupted; bool failed = false;
    try { source->Decode(&interrupted); } catch (const CreMcuFailure &failure) { failed = failure.code == CRE_CANCELLED; }
    assert(failed && interrupted.ended && interrupted.error); platform.cancel_at = SIZE_MAX;
    // 中文：超大固有尺寸只按显示目标输出，不分配固有尺寸彩色缓冲。
    // English: Huge intrinsic sizes render only at the display target without an intrinsic-size color bitmap.
    xml = "<svg width='100000' height='50000'><rect width='100000' height='50000'/></svg>";
    stream = LVCreateMemoryStream(xml.data(), xml.size(), true, LVOM_READ);
    source = LVCreateStreamImageSource(stream); assert(!source.isNull());
    SvgPixels large; assert(source->Decode(&large) && large.ended && !large.error);
}
int main(){
    Platform platform;auto cfg=config(platform);cfg.decode_image=decode;
    assert(cre_mcu_initialize(&cfg)==CRE_OK);
    for(bool png:{true,false}){
        uint8_t encoded[32]{};
        if(png){std::memcpy(encoded,"\x89PNG\r\n\x1a\n",8);encoded[11]=13;std::memcpy(encoded+12,"IHDR",4);encoded[19]=128;encoded[23]=64;}
        else{uint8_t jpeg[]={255,216,255,192,0,11,8,0,64,0,128,1,1,0,0,255,217};std::memcpy(encoded,jpeg,sizeof(jpeg));}
        auto source=LVCreateMemoryStream(encoded,sizeof(encoded),true,LVOM_READ);
        auto image=LVCreateStreamImageSource(source);assert(!image.isNull() && image->GetWidth()==128 && image->GetHeight()==64);
        assert(image->Decode(nullptr) && !calls);
        LVGrayDrawBuf buffer(64,32,8);buffer.Clear(0xffffff);buffer.Draw(image,0,0,64,32,false);
        assert(calls==1 && target_width==64 && target_height==32);
        for(int y=0;y<32;++y)for(int x=0;x<64;++x)assert(buffer.GetScanLine(y)[x]==(x<32?0:255));
        platform.fail_at=platform.allocations;bool failed=false;
        try{buffer.Draw(image,0,0,64,32,false);}catch(const CreMcuFailure &failure){assert(failure.code==CRE_MEMORY);failed=true;}
        assert(failed);platform.fail_at=SIZE_MAX;
        platform.cancel=true;failed=false;
        try{buffer.Draw(image,0,0,64,32,false);}catch(const CreMcuFailure &failure){assert(failure.code==CRE_CANCELLED);failed=true;}
        assert(failed);platform.cancel=false;
        image.Clear();source.Clear();calls=0;
    }
    svg(platform);
    cre_mcu_shutdown();assert(!platform.live && !platform.files);
    std::puts("PASS bounded host grayscale bridge, metadata-only probing, scaling, cancellation and allocation failure");
}
