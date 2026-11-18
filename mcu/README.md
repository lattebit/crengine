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
- 当前预算统计覆盖 FreeType、HarfBuzz、字形位图、度量缓存、共享字体源和灰阶临时缓冲。度量缓存分配失败可直接使用已计算的值；字形或整形分配失败不发布半成品像素。关闭最后一个字体后显式释放 HarfBuzz 全局缓存，支持重开和逐分配故障恢复。核心 DOM、C++ 对象及部分依赖分配尚未统一纳入该统计；宿主不能把此统计当作整个引擎的内存峰值或完整硬预算。
- JPEG/PNG 仅做流式尺寸探测，实际像素由 `decode_image` 平台回调使用给定预算分配器生成。回调返回连续八位灰阶，端口按目标尺寸逐行交给绘图层；不需要完整彩色位图。`cre_document_image` 可准备文档内图片，保持正文位置。

`cre_mcu_initialize` starts fonts; previews do not create a document. Font sources share four 4 KiB blocks per path and close at their final reference. `cre_document_open` admits one active document. EPUB indexes survive chapter changes without retaining chapter DOM nodes. `prepare` builds chapter block geometry and confirms an initial batch of at least three pages. `extend` resumes the retained line cursor, unfinished page, flows and deferred notes without changing delivered pages. Completion describes the current chapter. Block geometry still requires the whole chapter, and TXT still builds a full DOM; these stages are not yet streaming. The host owns packed 4bpp pixels and their borrowing lifetime. Close documents before fonts and platform context. Budget statistics cover FreeType, HarfBuzz, glyph bitmaps, metrics, shared sources and grayscale scratch buffers, **not all DOM, C++ or dependency allocations**. Optional metric-cache failures retain the computed result; failed glyph or shaping allocations never publish partial pixels. Shutdown explicitly frees HarfBuzz global caches after the final font reference, supporting reopen and allocation-failure recovery.

嵌入字体通过可选 `cache_font` 回调按需提取，回调分块读取解包/解混淆后的资源并返回普通文件路径。随后复用外部字体的四个 4 KiB 共享读取块，不创建整份字体内存副本。宿主必须把文档身份纳入缓存键，并在文件可能被使用时保护它；取消、I/O 和内存错误仍传播到请求。没有可用磁盘缓存时，`CRE_BUSY` 选择有界的原资源流回退，缓存同样跨字号共享。
Embedded fonts use optional `cache_font` extraction: the callback reads unpacked/deobfuscated resource chunks and returns a regular file path. Fonts then reuse the external-font path and its four shared 4 KiB blocks without a complete in-memory copy. The host must scope cache keys by document identity and protect files while they may be used. Cancellation, I/O and memory failures propagate to the request. `CRE_BUSY` selects bounded original-resource streaming when disk caching is unavailable, also sharing its cache across sizes.

MCU 嵌入字体不再套用上游整份驻留读取的 25 MB 限制；宿主决定磁盘提取容量。40 MiB 字体测试同时覆盖提取与资源流回退，单次源读取最多 4 KiB，跨字号像素与普通字体一致。
MCU embedded fonts no longer use the upstream 25 MB whole-file residency limit; the host governs disk extraction capacity. A 40 MiB font test covers extraction and resource-stream fallback, with source reads capped at 4 KiB and cross-size pixels identical to ordinary fonts.

`cre_mcu_build_identity()` 无初始化副作用，身份包含构建源码、依赖提交、目标 ABI、编译器、构建类型、裁剪选项和用户代理样式，可直接用作字体与布局缓存身份的一部分。修改编译选项时应同步修改 `crsetup.h.in` 或依赖构建入口，使身份随行为变化。

`cre_mcu_build_identity()` requires no initialization. Its source/dependency/ABI/compiler/build-type/configuration/style fingerprint belongs in font and layout cache keys. Keep build options in the checked build inputs so behavior changes invalidate the identity.

EPUB XHTML/HTML/CSS source bytes are independent of fonts/layout and share a bounded cache: eight entries, 512 KiB total, 256 KiB per entry. Oversized resources retain direct ZIP streaming. Source bytes use the tracked allocation budget, and readers pin their entries. `cre_document_source_cache` attaches host persistence; only explicit `cre_document_checkpoint` calls write dirty entries, retaining failures for retry. Layout changes preserve source data.

`cre_document_prefetch` 只预读指定 EPUB 章节源，不创建第二个 DOM、字体或页面，也不改变当前内容位置。仅使用剩余源缓存容量，不为预读淘汰热源；超大章节或预算不足直接跳过。取消或读盘失败归还未完成的数据，当前布局和借用像素保持有效。
`cre_document_prefetch` reads only the requested EPUB chapter source, without another DOM, font, page or position change. It uses spare source-cache capacity without evicting hot entries, skipping oversized chapters or insufficient budget. Cancellation or failed reads discard incomplete source data while preserving the current layout and borrowed pixels.

EPUB 的主语言只读取元数据下第一条 Dublin Core 语言，按命名空间 URI 识别并保留首条为空的语义。
Read only the first Dublin Core language directly under EPUB metadata, resolving namespace URIs and retaining an empty first value.

EPUB 导航使用临时资源查找表匹配章节，避免目录数量与章节数量相乘的扫描；遍历期间保留取消点。MCU 的默认路径分隔符固定为 `/`，ZIP 根目录导航不追加宿主平台路径。
EPUB navigation uses a temporary resource lookup instead of scanning every chapter for each entry, retaining cancellation checkpoints during traversal. MCU paths default to `/`; archive-root navigation never adds host-platform directory syntax.

EPUB 索引和正文支持 UTF-8、带或不带 BOM 的 UTF-16 XML。UTF-16 按字节序合并代理对，跨读取块保留未完成字符；无效代理单元替换为 U+FFFD，不吞掉下一字符。导航测试同时覆盖 NCX、嵌套 EPUB3 目录、回退与转义相对路径。
EPUB indexes and bodies accept UTF-8 and UTF-16 XML with or without a BOM. UTF-16 combines surrogate pairs using the detected byte order, retains incomplete characters across reads, and replaces invalid surrogate units with U+FFFD without consuming the next character. Navigation tests cover NCX, nested EPUB3 contents, fallback and escaped relative paths.

TXT 以固定 1 KiB 工作区识别 UTF-8 与 GBK；BOM 优先指定 Unicode 编码。无 BOM 时先验证完整 UTF-8，再接受有效 GBK，否则按 UTF-8 显示替代符。短文件、长 ASCII 前缀后的汉字和跨块字符都执行同一规则。UTF-8 无效标量替换为 U+FFFD，65536 字符长行不因位置回绕而丢失；这仍不等于有界 DOM 流式排版。
TXT detection uses fixed 1 KiB workspace, giving Unicode BOMs priority, then checking complete UTF-8 before accepting valid GBK; otherwise it decodes UTF-8 with replacements. Short files, Chinese after long ASCII prefixes and split characters share these rules. Invalid UTF-8 scalars become U+FFFD and 65536-character lines do not disappear through position overflow. This does not yet provide bounded streaming DOM layout.

TXT 目录识别简繁中文编号章节，包括中文数字和全角数字。有编号章节时，短正文不会仅因字数少就被误列为标题。
TXT contents recognize numbered simplified/traditional Chinese chapters, including Chinese and full-width numerals. When numbered chapters are present, short body lines are not promoted to headings solely because of their length.

非 EPUB 目录直接使用解析得到的结构锚点，不更新页码或触发正文排版；目录批次读取不产生字形文件读取。
Non-EPUB contents use parsed structural anchors directly, without updating page numbers or triggering body layout. Reading contents batches does not read glyph data.

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
- HarfBuzz 通过自定义分配器纳入预算；原本进程退出时执行的缓存清理由服务关闭执行。构建目录的 `hb-common_mcu.cc` 不跨关闭保留默认语言句柄，crengine 同时释放语言配置中的借用句柄。
- `cr3gui/data` 中的上游样式在配置时嵌入，保持 EPUB 章节分页、HTML 表格及列表语义；无需在设备上部署 CSS 文件。

FreeType 可变字体初始化失败时，构建目录中的 `ttgxvar_mcu.c` 修正空 `mmvar` 的清理；配置阶段校验唯一修改位置。预览同时返回按 `wght` 标签识别的字重轴能力，不把 Literata 的 `opsz` 当成字重。
The build applies a guarded `mmvar` cleanup fix to generated `ttgxvar_mcu.c`, checking the unique source anchor. Preview also reports weight-axis support by the `wght` tag rather than mistaking Literata’s `opsz` axis for weight.

静态字体缺少更细字重时使用最接近的可用字形，不用负向描边强行减细，避免宋体细横画断裂。较粗字重仍可合成；可变字体通过真实 `wght` 轴选择轮廓。
Static fonts use the nearest available design when a lighter weight is absent, without eroding thin serif strokes through negative outline synthesis. Heavier weights may still be synthesized; variable fonts select actual outlines through their `wght` axis.

Port copies retain their upstream licenses. The FreeType hash port fixes allocation-failure cleanup; utf8proc bridges its public `int *` state to a local `utf8proc_int32_t` on S31. Regenerate FriBidi tables with `python3 mcu/generate_fribidi.py` after upgrading its pinned sources. Upstream user-agent CSS is embedded at configuration time.

HarfBuzz uses the shared allocator and runs its exit-cache callbacks at service shutdown. Generated `hb-common_mcu.cc` avoids retaining a default language across shutdown; crengine also discards language configurations that borrow those handles.

## 可复跑测试 / Reproducible tests

```sh
cmake -S mcu -B build/mcu-test -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCRE_TEST_FONT=/absolute/path/to/a/font.ttf
cmake --build build/mcu-test
ctest --test-dir build/mcu-test --output-on-failure
```

测试生成六格式样本，检查有效灰阶内容、EPUB 逐章与完整排版像素对照、Markdown 直接 DOM 与 MD4C HTML 参考像素对照、C 接口锚点和链接、字体预览缺字/取消/I/O/分配故障清理。Markdown 对照显式统一 H1 分页规则，避免上游 `html-document` 样式条件造成格式间差异。需要检查 CJK 像素时应选择覆盖中英文的字体。

Fixtures cover six formats, meaningful grayscale content, chapter-versus-full EPUB pixels, direct Markdown DOM versus MD4C HTML pixels, C-port anchors/links, and preview fault cleanup. The Markdown comparison explicitly aligns H1 pagination to remove the upstream `html-document` format-specific difference. Use a font covering CJK and Latin when checking multilingual pixels.

C++ 分配失败测试覆盖文档打开与绘制的前 100 次分配及分散在全流程中的 64 个位置，并覆盖前 32 次和全流程 32 个位置的持续分配失败。错误必须回到 C 接口，关闭后释放资源；同一引擎中重新打开必须保持像素一致，不能靠关闭全局引擎恢复。可用 `cre_mcu_allocation_failure_test FONT FIXTURES --case OFFSET DRAW` 单独复现；`DRAW` 为 0 表示从打开开始，为 1 表示从绘制开始。设置 `CRE_TEST_ALLOCATION_TRACE=1` 打印失败栈。
C++ allocation tests inject one-shot failures into the first 100 allocations and 64 positions spanning document open/draw, plus persistent failures in the first 32 allocations and 32 positions spanning each operation. Errors must return through the C API and closing must release resources. Reopening in the same engine must retain identical pixels, without global shutdown for recovery. Reproduce one case with the command above: `DRAW=0` starts at open and `DRAW=1` at draw. Set `CRE_TEST_ALLOCATION_TRACE=1` for failure stacks.

MCU 解析器在 `OnStop()` 中完成可能分配内存的样式收尾；析构只释放未完成的栈节点。直接构造文档的调用方也必须调用 `OnStop()`。CSS 临时链和引用计数采用失败安全的所有权交接；字体注销不创建临时列表，文档注销后仍释放未持久化节点。以上错误清理不等同于全部引擎分配已计入硬预算。
MCU parsers finish potentially allocating style work in `OnStop()`; destruction only releases unfinished stack nodes. Direct document builders must call `OnStop()` too. CSS temporary chains and reference counters transfer ownership safely on failure; font unregistration allocates no temporary list, and mutable nodes are released after document unregistration. These cleanup guarantees do not imply that every engine allocation is accounted against the hard budget.

中文禁则测试检查真实字符坐标，覆盖十三种行宽下的行首行尾标点、组合音符、成对破折号和省略号及不可断空格。嵌入字体测试对照普通字体的多字号像素，并验证分块读取、共享读取缓存和失败清理。
Typography tests inspect real character positions across thirteen widths for opening/closing punctuation, combining marks, paired dashes/ellipses and nonbreaking spaces. Embedded-font tests compare ordinary-font pixels at multiple sizes and verify chunked reads, shared read caching and failure cleanup.

EPUB 资源表和阅读顺序各最多 32768 项，解析时在建立下一节点前拒绝超限输入。边界测试同时检查上限、超限清理与随后重开。这是条目数限制，不代表上限规模在 MCU 内存中一定可用。
EPUB manifests and spines admit at most 32768 entries each, rejecting excess input before creating the next node. Boundary tests cover the limit, rejected-document cleanup and reopening. This entry-count limit does not guarantee that a document at the limit fits MCU memory.

分页器可分别序列化固定行几何与续算游标，脚注及别名引用使用索引，恢复前校验摘要、长度和引用范围。测试在每次恢复后对照完整分页，并覆盖损坏、截断、取消、非线性流及跨页脚注。C 端口通过可选的布局记录回调接入宿主持久化。
The paginator serializes fixed line geometry separately from its continuation cursor, encoding footnotes and aliases as indices. Restoration validates checksums, lengths and reference ranges. Tests compare every restored run against complete pagination, including corruption, truncation, cancellation, nonlinear flows and spanning footnotes. The C port connects optional layout-record callbacks to host persistence.

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

布局缓存以 16 KiB 不可变块和最后发布的根记录保存 DOM、行几何与分页续算状态，不保存像素。单个逻辑布局文件上限 64 MiB；工作流使用一个块缓冲和至多 4096 个块键，计入受管预算。恢复按需校验块，不扫描目录或整个逻辑文件；定位目录为每页 9 字节，页对象和脚注按需解码；调用结束后只保留当前页及相邻已缓存页，未持久化的新页不会提前回收。布局键覆盖章节、全部排版选项、字体路径/长度/首尾 4 KiB 和构建身份；宿主还须按文档源身份隔离记录。相同元数据及首尾块的中部字体修改需要主动失效缓存。
Layout caches persist DOM, line geometry and pagination continuation as immutable 16 KiB blocks and a root published last, excluding pixels. Each logical layout file is limited to 64 MiB; one block buffer and at most 4096 keys count against managed memory. Restore validates blocks on access without scanning directories or the whole logical file. Positioning metadata uses nine bytes per page; page objects and footnotes decode on demand. After each call, retain only the current and neighboring cached pages; unsaved new pages are not reclaimed. Keys include chapter, all typography fields, font path/length/first and last 4 KiB, and build identity; hosts must additionally isolate records by source identity. A middle-only font change with identical metadata and endpoint blocks requires explicit invalidation.

只有 `cre_document_checkpoint` 发布根，成功后才回收旧块。读盘损坏会在打开或准备页面时重新从源加载一次；取消和内存错误不自动重试。写入失败丢弃工作布局，下一次准备从源重建，宿主已借用的像素保持不变。单个布局键只能有一个活动写入者；回调不得抛异常，返回写入失败时也可能已完成激活，因此不能提前删除新块。
Only `cre_document_checkpoint` publishes a root, reclaiming old blocks after success. Cache corruption during open or page preparation retries once from source; cancellation and memory failures do not retry. Write failure discards the working layout for reconstruction on the next preparation, preserving pixels already borrowed by the host. Each layout key permits one active writer. Callbacks must not throw; a reported write failure may occur after activation, so new blocks must not be removed prematurely.

确定页分别写入缓存，检查点仅追加新页，独立更新定位目录与续算状态。每章最多缓存 65535 页，超过时继续阅读但跳过布局持久化。逻辑记录以 128 字节对齐，底层仍按 16 KiB 合并读写；填充临时栈降至 512 字节。五千页测试验证末页只读取一条页记录、定位不解码其他页，并覆盖损坏、取消、读盘失败、回收与分配不足。
Confirmed pages use separate records; checkpoints append new pages while updating positioning metadata and continuation independently. Cache at most 65535 pages per chapter, skipping layout persistence above this count while allowing reading. Logical records align to 128 bytes while physical reads/writes coalesce into 16 KiB blocks; padding uses 512 bytes of temporary stack. A 5000-page test verifies a one-record final-page read and positioning without decoding other pages, covering corruption, cancellation, failed reads, reclamation and allocation failure.
