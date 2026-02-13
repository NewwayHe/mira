/* 块：OP_BLOCK */
#include "codegen.h"

/* 块作为值：push 块地址（供 for/while/if/when 消费） */
void gen_op_block(Op *o) {
	int L = new_label();
	emit("  jmp Lskip_%d\n", L);
	emit("Lblock_%d:\n", L);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(o->u.block);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", L);
	emit("  lea rax, [rel Lblock_%d]\n", L);
	emit_push_rax();
	type_stack[type_depth++] = TOS_INT;
}

/* 块作为语句：直接执行（如 sth: {params}{ body } 的 body） */
void gen_op_block_execute(Op *o) {
	int L = new_label();
	emit("  jmp Lskip_%d\n", L);
	emit("Lblock_%d:\n", L);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(o->u.block);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", L);
	emit("  sub rsp, 32\n");
	emit("  call Lblock_%d\n", L);
	emit("  add rsp, 32\n");
}
