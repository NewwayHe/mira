/* codegen 集成：gen_ops + gen_op 分发，并拉入各功能模块 */
#include "codegen.h"
#include <stdlib.h>
#include <string.h>

/* 各功能模块（同 TU；相对 codegen/ 目录） */
#include "state.c"
#include "emit.c"
#include "literal.c"
#include "block.c"
#include "list_literal.c"
#include "loops.c"
#include "words.c"

static bool is_block_consumer(Op *o) {
	if (!o) return false;
	if (o->kind == OP_IF) return true;
	if (o->kind != OP_WORD) return false;
	const char *n = o->u.word.name;
	size_t len = o->u.word.len;
	return (len == 2 && memcmp(n, "if", 2) == 0) ||
	       (len == 4 && memcmp(n, "when", 4) == 0);
}

void gen_ops(Op *list) {
	for (; list; list = list->next) gen_op(list, list->next);
}

void gen_op(Op *o, Op *next) {
	if (!o) return;
	switch (o->kind) {
	case OP_INT:
		gen_op_int(o);
		return;
	case OP_FLOAT:
		gen_op_float(o);
		return;
	case OP_CONST:
		gen_op_const(o);
		return;
	case OP_VAR:
		gen_op_var(o);
		return;
	case OP_STR:
		gen_op_str(o);
		return;
	case OP_WORD:
		gen_op_word(o);
		return;
	case OP_BLOCK:
		if (is_block_consumer(next))
			gen_op_block(o);
		else
			gen_op_block_execute(o);
		return;
	case OP_IF:
		gen_op_if(o);
		return;
	case OP_SWITCH:
		gen_op_switch(o);
		return;
	case OP_LIST_LITERAL:
		gen_op_list_literal(o);
		return;
	case OP_FOR_EXT:
		gen_op_for_ext(o);
		return;
	case OP_WHILE_INF:
		gen_op_while_inf(o);
		return;
	case OP_FOR_CSTYLE:
		gen_op_for_cstyle(o);
		return;
	case OP_FOR_RANGE:
		gen_op_for_range(o);
		return;
	case OP_EACH:
		gen_op_each(o);
		return;
	case OP_WHILE_COND:
		gen_op_while_cond(o);
		return;
	}
}

#include "program.c"
