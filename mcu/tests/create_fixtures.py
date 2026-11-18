#!/usr/bin/env python3
"""生成可复跑的六格式内容样本。/ Generate reproducible content samples in six formats."""
from pathlib import Path
import sys
import zipfile
import struct
import zlib

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
paragraph = "Anchor target. 中文排版，标点不能落在错误的位置。 Reading words with punctuation and shaping. "
body = "<h1>Layout fixture</h1>" + "".join(f"<p>{i}. {paragraph * 3}</p>" for i in range(32))
body += '<table><tr><th>One</th><th>Two</th></tr><tr><td>Cell A</td><td>Cell B</td></tr></table>'
body += '<p><a href="#note">Note link</a></p><p id="note">Footnote text.</p>'
html = '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Fixture</title></head><body>' + body + '</body></html>'
(out / "fixture.html").write_text(html)
# 中文：单段长正文验证续分页不会改变已交付页面或丢失字符。
# English: A long single paragraph checks resumed pagination for immutable delivered pages and complete text.
(out / "long-paragraph.html").write_text('<html><body><p>' + '中文测试。' * 2000 + '</p></body></html>')
# 复杂分页状态对照，不依赖设备字体设置。/ Complex pagination state comparison independent of device font settings.
stress = '<div class="float">Floating 中文 text ' + paragraph + '</div><ol start="7">'
stress += ''.join(f'<li><p>{paragraph * 2}<a href="#n{i % 3}">[{i % 3}]</a></p></li>' for i in range(36)) + '</ol>'
stress += '<table>' + ''.join(f'<tr><td>{paragraph * 2}</td><td>{paragraph}</td></tr>' for _ in range(12)) + '</table>'
stress += '<div class="overlap">' + paragraph * 4 + '</div><div class="flow"><p>' + paragraph * 6 + '</p></div>'
stress += ''.join(f'<aside class="note" id="n{i}"><p>Footnote {i}. {paragraph * (i + 3)}</p></aside>' for i in range(3))
(out / "pagination.html").write_text('<html xmlns="http://www.w3.org/1999/xhtml"><head><style>'
    '.float { float: right; width: 35%; margin: 1em; } li { margin-bottom: 0.4em; }'
    '.overlap { margin-top: -2em; } .flow { -cr-hint: non-linear; } .note { -cr-hint: footnote-inpage; font-size: 80%; }'
    '</style></head><body>' + stress + '</body></html>')
(out / "fixture.txt").write_text("Layout fixture\n\n" + "\n\n".join(paragraph * 3 for _ in range(32)))
for encoding in ("utf-8", "gbk", "utf-16", "utf-32"):
    (out / f"short-{encoding}.txt").write_bytes("中文\n".encode(encoding))
for name, text in [("english", "Chapter 1 Start\nhello\nChapter 2 End\nworld\n"),
                   ("chinese", "第一章 开始\n你好\n第一天的天气很好\n第二章 结束\n再见\n"),
                   ("traditional", "第壹卷 開始\n你好\n第２節 結束\n再見\n")]:
    (out / f"headings-{name}.txt").write_text(text)
(out / "many-headings.txt").write_text(''.join(f"Chapter {i}\nBody text.\n" for i in range(2200)))
(out / "fixture.fb2").write_text('<?xml version="1.0" encoding="UTF-8"?><FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0"><description><title-info><book-title>Fixture</book-title><lang>zh</lang></title-info></description><body><section><title><p>Layout fixture</p></title>' + ''.join('<p>' + paragraph * 3 + '</p>' for _ in range(32)) + '</section></body></FictionBook>')
(out / "fixture.rtf").write_text(r'{\rtf1\ansi\deff0 {\fonttbl {\f0 Times New Roman;}}\f0\fs24 ' + (r'Anchor target. Reading formatted text. \b Bold\b0  and \i italic\i0 .\par ' * 80) + '}')
(out / "fixture.md").write_text('# Layout fixture\n\n' + (paragraph * 3 + '\n\n') * 24 + '## Lists and tables\n\n- First **bold**\n- Second *emphasis*\n\n1. Ordered\n2. List\n\n| Left | Right |\n| :--- | ---: |\n| A | B |\n\n[Link](#note) and `inline code` &amp; entity.\n\n```text\n  code\n    indentation\n```\n\n<b>Raw HTML</b>\n\n<div>HTML block</div>\n')
with zipfile.ZipFile(out / "fixture.epub", "w", compression=zipfile.ZIP_DEFLATED) as book:
    book.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    book.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    book.writestr("OEBPS/content.opf", '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">mcu-test</dc:identifier><dc:title>Fixture</dc:title><dc:creator>Test</dc:creator><dc:language>zh</dc:language></metadata><manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/><item id="b" href="b.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" properties="nav" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
    book.writestr("OEBPS/a.xhtml", html)
    book.writestr("OEBPS/b.xhtml", html.replace("Layout fixture", "Second chapter"))
    book.writestr("OEBPS/nav.xhtml", '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol><li><a href="a.xhtml">First</a></li><li><a href="b.xhtml">Second</a></li></ol></nav></body></html>')

# 灰阶图片用于验证相对资源与命中区域。/ Grayscale fixture verifies relative resources and hit areas.
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
rows = (b"\0" + bytes([0] * 16 + [160] * 16)) * 24
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 32, 24, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
(out / "image.png").write_bytes(png)
(out / "image.html").write_text('<html><body><p><img src="image.png" width="128" height="96"/></p></body></html>')
with zipfile.ZipFile(out / "cover.epub", "w") as book:
    book.writestr("mimetype", "application/epub+zip")
    book.writestr("META-INF/container.xml", '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    book.writestr("content.opf", '<package version="3.0" xmlns="http://www.idpf.org/2007/opf"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Cover fixture</dc:title></metadata><manifest><item id="text" href="image.html" media-type="application/xhtml+xml"/><item id="cover" href="image.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="text"/></spine></package>')
    book.writestr("image.html", (out / "image.html").read_bytes())
    book.writestr("image.png", png)

# 中文：XML 声明不区分编码名大小写，UTF-16 字节序由 BOM 确定。
# English: XML encoding names are case-insensitive and UTF-16 byte order follows its BOM.
for codec in ('utf-8', 'utf-16-le', 'utf-16-be'):
    with zipfile.ZipFile(out / 'fixture.epub') as source, zipfile.ZipFile(out / ('xml-' + codec + '.epub'), 'w', zipfile.ZIP_DEFLATED) as book:
        for item in source.infolist():
            data = source.read(item)
            if item.filename.endswith(('.xml', '.opf', '.xhtml')):
                text = data.decode('utf-8')
                if text.startswith('<?xml'):
                    text = text.split('?>', 1)[1]
                encoding = 'UTF-8' if codec == 'utf-8' else 'UTF-16'
                data = ('<?xml version="1.0" encoding="' + encoding + '"?>' + text).encode(codec)
                data = (b'\xff\xfe' if codec == 'utf-16-le' else b'\xfe\xff' if codec == 'utf-16-be' else b'') + data
            book.writestr(item.filename, data, compress_type=zipfile.ZIP_STORED if item.filename == 'mimetype' else zipfile.ZIP_DEFLATED)

(out / "line-height.html").write_text('<html><body>' + '<p>ABC</p>' * 80 + '</body></html>')

# 中文：跨多个解压块的跳读覆盖图片大 APP 段及随机访问。
# English: Multi-block seeks cover large image APP segments and random access.
with zipfile.ZipFile(out / 'seek.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('bytes.bin', bytes((i * 37 + i // 251) & 255 for i in range(300000)))

(out / 'svg.html').write_text('<html><body><p><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="128" height="96" viewBox="0 0 32 24"><image width="32" height="24" xlink:href="image.png"/></svg></p></body></html>')

with zipfile.ZipFile(out / 'seek-short.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('bytes.bin', b'abc')
data = bytearray((out / 'seek-short.zip').read_bytes())
struct.pack_into('<I', data, 22, 300000)
struct.pack_into('<I', data, data.index(b'PK\x01\x02') + 24, 300000)
(out / 'seek-short.zip').write_bytes(data)

(out / 'inline-symbol.html').write_text('<html><head><style>img.symbol { height: 1em; }</style></head><body><p>AB<img class="symbol" src="image.png"/>CD</p></body></html>')

# 中文：首条 Dublin Core 语言按实际命名空间选择，不按前缀拼写猜测。
# English: Select the first Dublin Core language by its namespace, not its prefix spelling.
languages = [
    ('<dc:language>ja</dc:language><dc:language>en</dc:language>', 'ja'),
    ('<dc:language/><dc:language>en</dc:language>', ''),
    ('<other:language xmlns:other="urn:other">en</other:language><dc:language>ja</dc:language>', 'ja'),
    ('<language xmlns="http://purl.org/dc/elements/1.1/">ja</language>', 'ja'),
    ('<alt:language xmlns:alt="http://purl.org/dc/elements/1.1/">en</alt:language>', 'en'),
    ('<dc:language xmlns:dc="urn:other">en</dc:language><dc:language>ja</dc:language>', 'ja'),
    ('<other:language xmlns:other="urn:other">en</other:language>', ''),
    ('<meta><dc:language>en</dc:language></meta><dc:language>ja</dc:language>', 'ja'),
    ('<dc:language>en<![CDATA[-US]]></dc:language>', 'en-US'),
    ('<dc:language>en&#45;GB</dc:language>', 'en-GB'),
]
for i, (metadata, expected) in enumerate(languages):
    with zipfile.ZipFile(out / 'fixture.epub') as source, zipfile.ZipFile(out / f'language-{i}.epub', 'w', zipfile.ZIP_DEFLATED) as book:
        for item in source.infolist():
            data = source.read(item)
            if item.filename.endswith('.opf'):
                data = data.replace(b'<dc:language>zh</dc:language>', metadata.encode())
            book.writestr(item, data)

# 中文：迁移旧引擎有效目录场景；相对目录、转义路径、嵌套目录和无 BOM 的 UTF-16。
# English: Retain valid legacy navigation scenarios: relative/escaped paths, nested contents and BOM-less UTF-16.
for kind in ('ncx', 'nav', 'fallback'):
    for codec in ('utf-8', 'utf-16-le', 'utf-16-be'):
        container = '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OPS/pkg/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'
        items = ''.join(f'<item id="c{i}" href="../text/part%20{i}%26x.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, 5))
        items += '<item id="ncx" href="../toc/book.ncx" media-type="application/x-dtbncx+xml"/><item id="nav" href="../toc/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
        opf = '<package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="' + ('2.0' if kind == 'ncx' else '3.0') + '"><metadata><dc:title>Title &amp; 中文</dc:title></metadata><manifest>' + items + '</manifest><spine toc="ncx">' + ''.join(f'<itemref idref="c{i}"/>' for i in range(1, 5)) + '</spine></package>'
        points = '<navPoint><navLabel><text>Parent &amp; 中文😀</text></navLabel><content src="../text/part%201%26x.xhtml#start"/><navPoint><navLabel><text>Child &#84;wo</text></navLabel><content src="../text/part%202%26x.xhtml#start"/></navPoint></navPoint>'
        points += ''.join(f'<navPoint><navLabel><text>Chapter {i}</text></navLabel><content src="../text/part%20{i}%26x.xhtml#start"/></navPoint>' for i in (3, 4))
        ncx = '<!DOCTYPE ncx SYSTEM "https://invalid.example/never-read.dtd"><ncx><navMap>' + points + '</navMap></ncx>'
        links = '<li><a href="../text/part%201%26x.xhtml#start">Parent &amp; 中文😀</a><ol><li><a href="../text/part%202%26x.xhtml#start">Child &#84;wo</a></li></ol></li>'
        links += ''.join(f'<li><a href="../text/part%20{i}%26x.xhtml#start">Chapter {i}</a></li>' for i in (3, 4))
        nav = '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="landmarks"><a href="../text/part%201%26x.xhtml">WRONG</a></nav><nav epub:type="toc"><ol>' + links + '</ol></nav></body></html>'
        files = {'META-INF/container.xml': container, 'OPS/pkg/book.opf': opf, 'OPS/toc/book.ncx': ncx}
        if kind != 'fallback':
            files['OPS/toc/nav.xhtml'] = nav
        files.update({f'OPS/text/part {i}&x.xhtml': f'<html><body><p id="start">Body chapter {i}. Text &amp; 中文.</p></body></html>' for i in range(1, 5)})
        with zipfile.ZipFile(out / f'nav-{kind}-{codec}.epub', 'w', zipfile.ZIP_DEFLATED) as book:
            book.writestr('mimetype', 'application/epub+zip')
            for name, text in files.items():
                declaration = '<?xml version="1.0" encoding="' + ('UTF-8' if codec == 'utf-8' else 'UTF-16') + '"?>'
                book.writestr(name, (declaration + text).encode(codec))
            book.writestr('unused.ttf', b'x' * (4 * 1024 * 1024 + 1))

# 中文：预读只能使用空闲源缓存，不能挤掉热章节或打开超过单项上限的资源。
# English: Prefetch uses only spare source capacity without evicting hot chapters or opening oversized resources.
with zipfile.ZipFile(out / 'prefetch-capacity.epub', 'w', zipfile.ZIP_DEFLATED) as book:
    book.writestr('mimetype', 'application/epub+zip')
    book.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    book.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf"><manifest>' + ''.join(f'<item id="c{i}" href="{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(4)) + '</manifest><spine>' + ''.join(f'<itemref idref="c{i}"/>' for i in range(4)) + '</spine></package>')
    for i in range(4):
        book.writestr(f'{i}.xhtml', '<html><body><p>' + 'A' * ((200 if i < 3 else 300) * 1024) + '</p></body></html>')

# 中文：迁移旧包解析器的异常输入；恢复策略由新解析器决定，但不能读取包外资源或污染后续文档。
# English: Retain malformed-package scenarios; the new parser owns recovery policy, but must not read outside the package or contaminate later documents.
with zipfile.ZipFile(out / 'nav-ncx-utf-8.epub') as book:
    package_base = {name: book.read(name) for name in book.namelist() if name != 'unused.ttf'}
opf_path = 'OPS/pkg/book.opf'
container_path = 'META-INF/container.xml'
opf = package_base[opf_path].decode()
package_variants = {
    'missing-reference': {opf_path: opf.replace('idref="c2"', 'idref="missing"')},
    'duplicate-id': {opf_path: opf.replace('id="c2"', 'id="c1"')},
    'nul': {opf_path: opf.replace('</package>', '\0</package>')},
    'mismatched-tag': {opf_path: opf.replace('</manifest>', '</wrong>')},
    'multiple-roots': {opf_path: opf + '<extra/>'},
    'broken-ncx': {'OPS/toc/book.ncx': '<ncx><broken></ncx>'},
    'trailing-ncx': {'OPS/toc/book.ncx': package_base['OPS/toc/book.ncx'].decode() + '<broken>'},
    'internal-dtd': {opf_path: opf.replace('<package ', '<!DOCTYPE package [<!ENTITY external SYSTEM "file:///never-open-this-font-test">]><package ').replace('Title &amp; 中文', '&external;')},
    'truncated-utf16': {container_path: b'\xff\xfe<\x00c'},
    'surrogate-utf16': {container_path: b'\xff\xfe' + '<container>'.encode('utf-16-le') + b'\x00\xd8' + '</container>'.encode('utf-16-le')},
    'empty-container': {container_path: '<container/>'},
    'escape-container': {container_path: package_base[container_path].replace(b'OPS/pkg/book.opf', b'../../outside.opf')},
}
for name, href in [('escape-path', '../../../outside.xhtml'), ('url-path', 'https://example.invalid/body.xhtml'),
                   ('escaped-url-path', 'https%3A//example.invalid/body.xhtml'), ('bad-percent', '../text/%XX.xhtml')]:
    package_variants[name] = {opf_path: opf.replace('../text/part%201%26x.xhtml', href)}
for name, changes in package_variants.items():
    with zipfile.ZipFile(out / f'package-{name}.epub', 'w', zipfile.ZIP_DEFLATED) as book:
        for path, data in {**package_base, **changes}.items():
            book.writestr(path, data)
