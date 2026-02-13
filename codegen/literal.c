/* 字面量与变量：OP_INT, OP_FLOAT, OP_CONST, OP_VAR, OP_STR */
#include "codegen.h"
#include <stdio.h>

void gen_op_int(Op *o) {
	emit_push_imm(o->u.i);
	type_stack[type_depth++] = TOS_INT;
}

void gen_op_float(Op *o) {
	int id = new_label();
	fprintf(comp->out, "section .data\n");
	fprintf(comp->out, "dbl.%d: dq %.17e\n", id, o->u.d);
	fprintf(comp->out, "section .text\n");
	emit("  mov rax, [rel dbl.%d]\n", id);
	emit_push_rax();
	type_stack[type_depth++] = TOS_FLOAT;
}

void gen_op_const(Op *o) {
	int slot = o->u.const_slot;
	ConstKind k = g_prog->const_kinds[slot];
	if (k == CONST_INT) {
		emit_push_imm(g_prog->const_ints[slot]);
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (k == CONST_DOUBLE) {
		emit("  mov rax, [rel const_dbl.%d]\n", slot);
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	emit("  lea rax, [rel const_str.%d]\n", slot);
	emit_push_rax();
	type_stack[type_depth++] = TOS_STR;
}

void gen_op_var(Op *o) {
	last_var_slot = o->u.var_slot;
	emit("  lea rax, [rel mira_vars + %d*8]\n", o->u.var_slot);
	emit_push_rax();
	type_stack[type_depth++] = var_types[o->u.var_slot];
}

void gen_op_str(Op *o) {
	int id = new_label();
	const char *s = o->u.str.s;
	size_t len = o->u.str.len;
	fprintf(comp->out, "section .data\n");
	fprintf(comp->out, "str.%d: db ", id);
	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		fprintf(comp->out, "%u%s", (unsigned)ch, i + 1 < len ? "," : "");
	}
	fprintf(comp->out, ",0\nsection .text\n");
	emit("  lea rax, [rel str.%d]\n", id);
	emit_push_rax();
	type_stack[type_depth++] = TOS_STR;
}
