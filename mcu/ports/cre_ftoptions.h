// 中文：保留上游配置入口，关闭 MCU 不使用的容器与系统环境访问。
// English: Retain upstream options, disabling unused containers and system environment access.
#include <freetype/config/ftoption.h>
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_SVG
// 中文：字体只使用共享文件流；不接受由调用方逐字形注入的数据。
// English: Fonts use shared file streams, never caller-supplied incremental glyph data.
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_RENDER_POOL_SIZE
// 中文：栅格器按带细分，缩小栈池不改变像素结果。
// English: Banded rasterization preserves pixels with a smaller stack pool.
#define FT_RENDER_POOL_SIZE 4096L
// 中文：保持原 MCU 后端的无 hinting 灰阶路径，省去字体字节码解释器和自动 hinting 工作区。
// English: Retain the previous MCU backend's unhinted gray path, removing font bytecode and autohint workspaces.
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#undef TT_CONFIG_OPTION_SUBPIXEL_HINTING
