#ifndef EPUBFMT_H
#define EPUBFMT_H

#include "crsetup.h"

#include "../include/lvtinydom.h"

/// 中文：与正文 DOM 无关的 EPUB 索引；持有压缩目录及解混淆状态，可重复加载章节。
/// English: DOM-independent EPUB index retaining the ZIP directory and deobfuscation state across chapter loads.
struct EpubIndex {
    struct Chapter {
        lString32 href, mediaType, id; ///< 原始资源名、类型和标识 / Original resource name, type and identifier
        bool xhtml = false, nonlinear = false; ///< 文档类型及阅读顺序 / Document type and reading order
    };
    struct Toc {
        lString32 title, href; ///< UTF-32 标题和已解析资源路径 / UTF-32 title and resolved resource path
        int chapter = -1, depth = 0; ///< 目标章节及层级 / Destination chapter and depth
    };
    LVContainerRef archive; ///< 共享受管压缩文件 / Shared managed archive
    CRPropRef properties; ///< 复制的元数据 / Copied metadata
    LVArray<Chapter> chapters; ///< 正文顺序，不持有 DOM 节点 / Reading order without DOM nodes
    LVArray<Toc> toc; ///< 不依赖正文已加载的目录 / Contents independent of loaded body nodes
    lString32 codeBase, navHref, ncxHref, pageMapHref, pageMapSource; ///< 索引资源路径 / Index resource paths
    bool ready = false, epub3 = false, unsupportedEncryption = false; ///< 完成及加密状态 / Completion and encryption state
};

bool DetectEpubFormat( LVStreamRef stream, LVContainerRef *archive=NULL );
bool ImportEpubDocument( LVStreamRef stream, ldomDocument * doc, LVDocViewCallback * progressCallback,
    CacheLoadingCallback * formatCallback, bool metadataOnly = false,
    const elem_def_t * node_scheme=NULL, const attr_def_t * attr_scheme=NULL, const ns_def_t * ns_scheme=NULL,
    EpubIndex *index=NULL, int chapter=-1);
lString32 EpubGetRootFilePath( LVContainerRef m_arc );
LVStreamRef GetEpubCoverpage(LVContainerRef arc);


#endif // EPUBFMT_H
