/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：验证纯文本短文件、编码边界和超长行不会丢字。
 * English: Verify short plain text, encoding boundaries and long lines retain every character.
 */
#include "platform.h"
#include "cre_mcu_internal.h"
#include "lvxml.h"
#include "lvtinydom.h"
#include <memory>
#include <string>

struct Text : LVXMLParserCallback {
    std::u32string body;
    bool inside = false;
    void OnStop() override {}
    ldomNode *OnTagOpen(const lChar32 *, const lChar32 *name) override {
        if (std::u32string(name) == U"body") inside = true;
        return nullptr;
    }
    void OnTagBody() override {}
    void OnTagClose(const lChar32 *, const lChar32 *name, bool = false) override {
        if (std::u32string(name) == U"body") inside = false;
    }
    void OnAttribute(const lChar32 *, const lChar32 *, const lChar32 *) override {}
    void OnText(const lChar32 *text, int length, lUInt32) override {
        if (inside) for (int i = 0; i < length; ++i) if (text[i] > 32 && text[i] != 0xfeff) body += text[i];
    }
    bool OnBlob(lString32, const lUInt8 *, int) override { return false; }
};

static bool check(const char *name, const std::string &input, const std::u32string &expected) {
    auto stream = LVCreateMemoryStream(const_cast<char *>(input.data()), input.size(), true);
    Text output;
    std::unique_ptr<LVFileFormatParser> parser(new LVTextParser(stream, &output, false));
    if (!parser->CheckFormat()) {
        parser.reset(new LVTextRobustParser(stream, &output, false));
        assert(parser->CheckFormat());
    }
    assert(parser->Parse());
    if (output.body != expected) {
        std::fprintf(stderr, "%s: expected=%zu actual=%zu first=%x\n", name, expected.size(), output.body.size(), output.body.empty() ? 0 : unsigned(output.body[0]));
        return false;
    }
    return true;
}

int main() {
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    bool ok = true;
    ok &= check("empty", "", U"");
    ok &= check("short UTF-8", "中文\n", U"中文");
    ok &= check("short GBK", "\xd6\xd0\xce\xc4\n", U"中文");
    ok &= check("BOM malformed UTF-8", "\xef\xbb\xbf" "abc\xff\nend", U"abc\ufffdend");
    ok &= check("UTF-8 invalid scalars", "\xef\xbb\xbf" "\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80" "end", std::u32string(9, 0xfffd) + U"end");
    ok &= check("UTF-8 incomplete EOF", "\xef\xbb\xbf" "end\xe4\xb8", U"end\ufffd\ufffd");
    ok &= check("English headings", "Chapter 1 Start\nhello\nChapter 2 End\nworld\n", U"Chapter1StarthelloChapter2Endworld");
    ok &= check("Chinese headings", "第一章 开始\n你好\n第二章 结束\n再见\n", U"第一章开始你好第二章结束再见");
    std::string long_line;
    for (unsigned i = 0; i < 100000; ++i) long_line += "中";
    ok &= check("300K UTF-8", long_line, std::u32string(100000, U'中'));
    for (size_t position : {8191u, 16383u, 65535u}) {
        auto prefix = std::string(position, 'a'); auto expected = std::u32string(position, U'a') + U"中";
        ok &= check("UTF-8 boundary", prefix + "中\n", expected);
        ok &= check("GBK boundary", prefix + "\xd6\xd0\n", expected);
        ok &= check("UTF-8 four-byte boundary", prefix + "\xf0\x9f\x93\x96\n", std::u32string(position, U'a') + U"\U0001f4d6");
    }
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
    assert(ok);
}
