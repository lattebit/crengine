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
(out / "fixture.txt").write_text("Layout fixture\n\n" + "\n\n".join(paragraph * 3 for _ in range(32)))
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
