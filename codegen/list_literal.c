/* 列表字面量：OP_LIST_LITERAL */
#include "codegen.h"

void gen_op_list_literal(Op *o) {
	int size = o->u.list_literal.size;
	int temp_slot = o->u.list_literal.temp_slot;
	Op *elements = o->u.list_literal.elements;
	emit_push_imm((int64_t)size);
	if (type_depth > 0) type_depth--;
	emit_pop_rax();
	emit("  mov rcx, rax\n");
	emit("  sub rsp, 32\n");
	emit("  call mira_list_new\n");
	emit("  add rsp, 32\n");
	emit_push_rax();
	emit("  lea rax, [rel mira_vars + %d*8]\n", temp_slot);
	emit_push_rax();
	type_stack[type_depth++] = TOS_INT;
	{
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  mov [rcx], rax\n");
	}
	int idx = 0;
	for (Op *e = elements; e; e = e->next, idx++) {
		gen_op(e, e->next);
		emit("  lea rax, [rel mira_vars + %d*8]\n", temp_slot);
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		{
			int t = type_depth > 0 ? type_stack[type_depth - 1] : TOS_INT;
			type_depth--;
			emit_pop_rax();
			emit("  mov rax, [rax]\n");
			emit_push_rax();
			type_stack[type_depth++] = t;
		}
		emit_push_imm((int64_t)idx);
		type_stack[type_depth++] = TOS_INT;
		type_depth -= 3;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  mov r8, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_set\n");
		emit("  add rsp, 32\n");
	}
	emit("  lea rax, [rel mira_vars + %d*8]\n", temp_slot);
	emit_push_rax();
	{
		int t = type_depth > 0 ? type_stack[type_depth - 1] : TOS_INT;
		type_depth--;
		emit_pop_rax();
		emit("  mov rax, [rax]\n");
		emit_push_rax();
		type_stack[type_depth++] = t;
	}
}
