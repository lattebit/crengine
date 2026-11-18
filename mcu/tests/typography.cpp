/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：检查真实排版的中文禁则、组合字符与不可断空格，不复制断行算法。
 * English: Inspect real layout for Chinese punctuation, combining marks and nonbreaking spaces without duplicating line-breaking logic.
 */
#include "platform.h"
#include "lvdocview.h"
#include "lvfntman.h"
#include "cre_stylesheets.h"
#include <string>
#include <vector>

static std::vector<lvRect> positions(const lString32 &text, unsigned width) {
    LVDocView view;
    view.setDefaultFontFace(lString8("typography")); view.setStatusFontFace(lString8("typography"));
    view.setFontSize(24); view.setPageHeaderInfo(0); view.setShowCover(false);
    view.setPageMargins(lvRect(0, 0, 0, 0)); view.setViewMode(DVM_PAGES, 1); view.Resize(width, 640);
    view.setStyleSheet(lString8(cre_styles::html) + "body, p { margin: 0; padding: 0; text-indent: 0; text-align: left; }");
    auto source = lString8("<html lang='zh'><head><title>Typography</title></head><body><p id='sample'>") + UnicodeToUtf8(text) + "</p></body></html>";
    assert(view.LoadDocument(LVCreateMemoryStream(const_cast<char *>(source.c_str()), source.length(), true)));
    view.Render();
    auto *paragraph = view.getDocument()->getElementById(U"sample"); assert(paragraph && paragraph->getChildCount() == 1);
    auto *node = paragraph->getChildNode(0); assert(node->isText() && node->getText() == text);
    std::vector<lvRect> result(text.length());
    for (int i = 0; i < text.length(); ++i) assert(ldomXPointer(node, i).getRectEx(result[i]));
    return result;
}
int main(int argc, char **argv) {
    assert(argc == 2); Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    assert(fontMan->RegisterExternalFont(-1, Utf8ToUnicode(argv[1]), lString8("typography"), 400, 100, false));
    lString32 text(U"天地，宇宙（洪荒）日月。山川；草木！日月？「天地」【宇宙】");
    unsigned wrapped = 0;
    for (unsigned width = 72; width <= 216; width += 12) {
        auto at = positions(text, width); wrapped += at.front().top != at.back().top;
        for (int i = 0; i < text.length(); ++i) {
            if (lString32(U"，。；！？）】」").pos(text.substr(i, 1)) >= 0) {
                if (i && at[i].top != at[i - 1].top) std::fprintf(stderr, "punctuation width=%u char=%x y=%d previous=%d\n", width, unsigned(text[i]), at[i].top, at[i - 1].top);
                assert(i && at[i].top == at[i - 1].top);
            }
            if (lString32(U"（【「").pos(text.substr(i, 1)) >= 0) assert(i + 1 < text.length() && at[i].top == at[i + 1].top);
        }
        lString32 joined(U"天地A\u0301宇宙——洪荒……日月A\u00a0B山川"); auto clusters = positions(joined, width);
        for (int i = 1; i < joined.length(); ++i) {
            if (joined[i] == U'\u0301' || ((joined[i] == U'—' || joined[i] == U'…') && joined[i - 1] == joined[i]))
                assert(clusters[i].top == clusters[i - 1].top);
            if (joined[i] == U'\u00a0') assert(clusters[i - 1].top == clusters[i].top && clusters[i].top == clusters[i + 1].top);
        }
    }
    assert(wrapped == 13);
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
    std::puts("Chinese punctuation, combining marks, paired dashes/ellipses and NBSP across thirteen widths passed");
}
