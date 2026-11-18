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
static unsigned calls;
static uint32_t target_width,target_height;
static int decode(void *,const uint8_t *,size_t,uint32_t width,uint32_t height,const cre_mcu_image_memory *memory,cre_mcu_bitmap *out){
    ++calls;target_width=width;target_height=height;
    out->pixels=static_cast<uint8_t *>(memory->allocate(size_t(width)*height));if(!out->pixels)return CRE_MEMORY;
    out->width=width;out->height=height;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)out->pixels[y*width+x]=x<width/2?0:255;
    return CRE_OK;
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
    cre_mcu_shutdown();assert(!platform.live && !platform.files);
    std::puts("PASS bounded host grayscale bridge, metadata-only probing, scaling, cancellation and allocation failure");
}
