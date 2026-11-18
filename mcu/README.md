# MCU 静态端口 / MCU static port

本目录面向固件提供 `crengine::mcu`，不构建桌面前端。所有引擎调用必须由同一个工作任务串行执行。平台通过 `cre_mcu_platform_t` 提供受管只读文件、内存和协作取消；异常在 C 端口内转换为错误码。

This directory provides `crengine::mcu` without desktop frontends. A single worker must serialize all engine calls. `cre_mcu_platform_t` supplies managed read-only files, allocation and cooperative cancellation. C entry points contain exceptions and return error codes.

## 构建 / Build

依赖为固定提交的嵌套 submodule，配置过程中不会下载或修改它们。
Dependencies are pinned nested submodules; configuration neither downloads nor modifies them.

```sh
git submodule update --init --recursive
cmake -S mcu -B build/mcu -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_TESTING=OFF
cmake --build build/mcu
```

固件通过 `add_subdirectory(path/to/crengine/mcu)` 引入，链接 `crengine::mcu`。所需 FreeType、HarfBuzz、FriBidi、libunibreak、utf8proc、zlib、xxHash、MD4C 均由该入口提供静态目标；NanoSVG 为引擎内的头文件实现。禁止另外编译同一依赖副本。

Firmware uses `add_subdirectory(path/to/crengine/mcu)` and links `crengine::mcu`. This entry owns the FreeType, HarfBuzz, FriBidi, libunibreak, utf8proc, zlib, xxHash and MD4C static targets. NanoSVG is included in the engine translation unit. Do not compile duplicate copies of these dependencies.

使用交叉工具链时关闭主机测试。S31 使用 ESP-IDF 6.1 配套的 RISC-V GCC 15.2 工具链和 `rv32imafc/ilp32f`。静态库成功编译不能替代完整固件链接或实机测量。

Disable host tests for cross compilation. S31 uses the RISC-V GCC 15.2 toolchain bundled with ESP-IDF 6.1 and `rv32imafc/ilp32f`. A successful static build does not replace firmware linking or hardware measurement.

## 接口与资源 / Interfaces and resources

- `cre_mcu_initialize` 初始化字体后端；`cre_mcu_font_preview` 只使用字体，不创建文档。字体源按路径共享四个 4 KiB 读取块；最后一个引用释放时关闭源文件。字体预览引用在成功、失败和取消后全部归还。
- `cre_document_open` 创建唯一活动文档。EPUB 的 OPF、ZIP 目录、解混淆状态、spine 和导航目录保存在独立 `EpubIndex` 中，正文按章节加载。其他格式保留其解析所需 DOM。
- EPUB 的 XHTML/HTML/CSS 源缓存独立于字体和布局，至多八项、合计 512 KiB、单项 256 KiB。超限资源仍流式读取原 ZIP；源字节计入统一分配预算，在途读者保护其引用。`cre_document_source_cache` 接入宿主持久化，`cre_document_checkpoint` 才写脏项；失败可以重试，排版变化不会清空源缓存。
- `cre_document_prepare` 准备当前章节的块布局，首批只确定至少三页；后续翻页按需推进。`cre_document_extend` 保留分页器的行游标、未完成页、文档流和延后脚注，不重新分页已交付内容。`complete` 仅表示当前章节结束。当前块布局准备仍覆盖整个章节，TXT 仍构造完整 DOM；这两个阶段尚不是流式续算。分页、字体和图像操作均不得在 UI 绘制回调执行。
- 像素缓冲由调用者分配和管理：白色为 15，左像素在高半字节。上游 `LVGrayDrawBuf` 的 4/8 位灰阶内部仍为一字节一像素，端口使用可释放的灰阶工作区再打包。不得把半字节输出缓冲直接传给该上游对象。
- `cre_document_toc`、`resolve`、`prepare` 使用章节和内容路径，不使用全书页号作为持久进度。宿主借用像素期间必须保证缓冲不被重新使用。`margin` 控制左右和顶部，`bottom_margin` 独立控制底部；零表示不预留额外页底空间。
- 关闭顺序为文档、字体后端、平台上下文。取消检查会抛出内部异常，不能从平台的 C 回调抛出异常。
- 当前预算统计覆盖 FreeType、共享字体源和灰阶临时缓冲。核心 DOM、C++ 对象及部分依赖分配尚未统一纳入该统计；宿主不能把此统计当作整个引擎的内存峰值或完整硬预算。
- JPEG/PNG 仅做流式尺寸探测，实际像素由 `decode_image` 平台回调使用给定预算分配器生成。回调返回连续八位灰阶，端口按目标尺寸逐行交给绘图层；不需要完整彩色位图。`cre_document_image` 可准备文档内图片，保持正文位置。

`cre_mcu_initialize` starts fonts; previews do not create a document. Font sources share four 4 KiB blocks per path and close at their final reference. `cre_document_open` admits one active document. EPUB indexes survive chapter changes without retaining chapter DOM nodes. `prepare` builds chapter block geometry and confirms an initial batch of at least three pages. `extend` resumes the retained line cursor, unfinished page, flows and deferred notes without changing delivered pages. Completion describes the current chapter. Block geometry still requires the whole chapter, and TXT still builds a full DOM; these stages are not yet streaming. The host owns packed 4bpp pixels and their borrowing lifetime. Close documents before fonts and platform context. Budget statistics currently cover FreeType, shared sources and grayscale scratch buffers, **not all DOM, C++ or dependency allocations**.

`cre_mcu_build_identity()` 无初始化副作用，身份包含构建源码、依赖提交、裁剪选项和用户代理样式，可直接用作字体与布局缓存身份的一部分。修改编译选项时应同步修改 `crsetup.h.in` 或依赖构建入口，使身份随行为变化。

`cre_mcu_build_identity()` requires no initialization. Its source/dependency/configuration/style fingerprint belongs in font and layout cache keys. Keep build options in the checked build inputs so behavior changes invalidate the identity.

EPUB XHTML/HTML/CSS source bytes are independent of fonts/layout and share a bounded cache: eight entries, 512 KiB total, 256 KiB per entry. Oversized resources retain direct ZIP streaming. Source bytes use the tracked allocation budget, and readers pin their entries. `cre_document_source_cache` attaches host persistence; only explicit `cre_document_checkpoint` calls write dirty entries, retaining failures for retry. Layout changes preserve source data.

`cre_document_prefetch` 只预读指定 EPUB 章节源，不创建第二个 DOM、字体或页面，也不改变当前内容位置。仅使用剩余源缓存容量，不为预读淘汰热源；超大章节或预算不足直接跳过。取消或读盘失败归还未完成的数据，当前布局和借用像素保持有效。
`cre_document_prefetch` reads only the requested EPUB chapter source, without another DOM, font, page or position change. It uses spare source-cache capacity without evicting hot entries, skipping oversized chapters or insufficient budget. Cancellation or failed reads discard incomplete source data while preserving the current layout and borrowed pixels.

EPUB 的主语言只读取元数据下第一条 Dublin Core 语言，按命名空间 URI 识别并保留首条为空的语义。
Read only the first Dublin Core language directly under EPUB metadata, resolving namespace URIs and retaining an empty first value.

EPUB 索引和正文支持 UTF-8、带或不带 BOM 的 UTF-16 XML。UTF-16 按字节序合并代理对，跨读取块保留未完成字符；无效代理单元替换为 U+FFFD，不吞掉下一字符。导航测试同时覆盖 NCX、嵌套 EPUB3 目录、回退与转义相对路径。
EPUB indexes and bodies accept UTF-8 and UTF-16 XML with or without a BOM. UTF-16 combines surrogate pairs using the detected byte order, retains incomplete characters across reads, and replaces invalid surrogate units with U+FFFD without consuming the next character. Navigation tests cover NCX, nested EPUB3 contents, fallback and escaped relative paths.

`margin` controls the top and both sides; `bottom_margin` is independent, with zero reserving no extra space below the last complete line.

JPEG/PNG dimensions are probed from streams. The optional `decode_image` callback returns contiguous grayscale bytes through the supplied budgeted allocator. The port emits target-sized rows without a full color bitmap; `cre_document_image` prepares a resource without advancing the body anchor.

简单的全画布 SVG 图片包装复用同一灰阶图片后端；矢量 SVG 按目标尺寸使用八行 RGBA 临时条带。书内 CSS 决定图片大小，端口关闭额外的旧式图片放大，避免行内符号被放大两次。压缩资源的跳读以绝对解压位置推进，截断流返回失败。
Simple full-canvas SVG image wrappers reuse the grayscale decoder; vector SVG uses eight temporary RGBA rows at the target size. Authored CSS determines image dimensions, with legacy extra zoom disabled to avoid doubling inline symbols. Compressed-resource seeks advance to absolute decoded positions and fail on truncated streams.

## 裁剪与维护 / Trimming and maintenance

`crsetup.h.in` 关闭系统字体发现、mmap、信号处理、彩色整页后备缓冲和桌面路径；源文件列表排除办公与旧格式导入器。JPEG/PNG/WebP/GIF 的上游软件库不参与本入口，平台图像接入必须单独提供。字体后端保留 TrueType/CFF/SFNT、HarfBuzz 整形与灰阶光栅，关闭原生 hinting、autohint、彩色字体和压缩字体容器。

The build excludes system font discovery, mmap, signal handling, color page backbuffers, desktop paths, office and legacy importers. Upstream JPEG/PNG/WebP/GIF libraries are disabled; platform image integration is separate. Fonts retain TrueType/CFF/SFNT, HarfBuzz shaping and grayscale rasterization, without native/autohint hinting, color fonts or compressed font containers.

- `ports/ftbase.c` 和 `fthash.c` 来自固定 FreeType 提交，保留原许可；后者处理分配失败后的空表清理。`cre_ftoptions.h` / `cre_ftmodule.h` 仅配置模块，不修改第三方检出。
- `ports/utf8proc.c` 来自固定 utf8proc 提交，修正 S31 上 `int32_t` 为 `long` 时与 `int *` 状态接口的类型不一致。
- `ports/fribidi` 表由同一 submodule 的 Unicode 输入生成，更新版本时运行 `python3 mcu/generate_fribidi.py`，不要手工改表。
- FriBidi 只编译 Unicode 排序需要的模块。`ports/harfbuzz.cc` 在加载标准库后隔离 Picolibc `_S` 宏，避免污染 HarfBuzz 的字符串表宏。
- `cr3gui/data` 中的上游样式在配置时嵌入，保持 EPUB 章节分页、HTML 表格及列表语义；无需在设备上部署 CSS 文件。

FreeType 可变字体初始化失败时，构建目录中的 `ttgxvar_mcu.c` 修正空 `mmvar` 的清理；配置阶段校验唯一修改位置。预览同时返回按 `wght` 标签识别的字重轴能力，不把 Literata 的 `opsz` 当成字重。
The build applies a guarded `mmvar` cleanup fix to generated `ttgxvar_mcu.c`, checking the unique source anchor. Preview also reports weight-axis support by the `wght` tag rather than mistaking Literata’s `opsz` axis for weight.

静态字体缺少更细字重时使用最接近的可用字形，不用负向描边强行减细，避免宋体细横画断裂。较粗字重仍可合成；可变字体通过真实 `wght` 轴选择轮廓。
Static fonts use the nearest available design when a lighter weight is absent, without eroding thin serif strokes through negative outline synthesis. Heavier weights may still be synthesized; variable fonts select actual outlines through their `wght` axis.

Port copies retain their upstream licenses. The FreeType hash port fixes allocation-failure cleanup; utf8proc bridges its public `int *` state to a local `utf8proc_int32_t` on S31. Regenerate FriBidi tables with `python3 mcu/generate_fribidi.py` after upgrading its pinned sources. Upstream user-agent CSS is embedded at configuration time.

## 可复跑测试 / Reproducible tests

```sh
cmake -S mcu -B build/mcu-test -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCRE_TEST_FONT=/absolute/path/to/a/font.ttf
cmake --build build/mcu-test
ctest --test-dir build/mcu-test --output-on-failure
```

测试生成六格式样本，检查有效灰阶内容、EPUB 逐章与完整排版像素对照、Markdown 直接 DOM 与 MD4C HTML 参考像素对照、C 接口锚点和链接、字体预览缺字/取消/I/O/分配故障清理。Markdown 对照显式统一 H1 分页规则，避免上游 `html-document` 样式条件造成格式间差异。需要检查 CJK 像素时应选择覆盖中英文的字体。

Fixtures cover six formats, meaningful grayscale content, chapter-versus-full EPUB pixels, direct Markdown DOM versus MD4C HTML pixels, C-port anchors/links, and preview fault cleanup. The Markdown comparison explicitly aligns H1 pagination to remove the upstream `html-document` format-specific difference. Use a font covering CJK and Latin when checking multilingual pixels.

Linux sanitizer 构建额外传入以下三个选项，然后运行相同 CTest：
For Linux sanitizer builds, add these options and run the same CTest suite:

```sh
-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
```

所有第三方代码及生成表继续遵守原始许可；分发静态固件时需要同时遵守 crengine 的 GPL 条款以及各依赖许可。
Retain upstream licenses for dependencies and generated tables. Static firmware distribution must comply with crengine's GPL terms and each dependency's license.

使用可变字体重复同一故障矩阵：`build/mcu-test/cre_mcu_font_test /path/to/variable.ttf variable`。
Repeat the fault matrix with a variable font using the command above.
