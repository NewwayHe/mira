/* Mira codegen 入口：功能已拆分到 codegen/ 目录，由 index.c 集成 */
#include "mira.h"

/* 拉入 codegen 目录下所有模块（index.c 会 include state/emit/literal/block/list_literal/words，再定义 gen_ops/gen_op，最后 include program.c） */
#include "codegen/index.c"
