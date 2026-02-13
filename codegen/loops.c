/* OP_FOR_EXT, OP_WHILE_INF, OP_FOR_CSTYLE, OP_FOR_RANGE, OP_EACH, OP_WHILE_COND */
#include "codegen.h"
#include <string.h>

/* 起始 结束 for { body } 或 变量 起始 结束 for { body }：后者变量可修改 */
void gen_op_for_range(Op *o) {
	int64_t start = o->u.for_range.start;
	int64_t end = o->u.for_range.end;
	Op *body = o->u.for_range.body;
	char *var = o->u.for_range.var;
	size_t var_len = o->u.for_range.var_len;
	int L = new_label();
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	if (var && var_len > 0) {
		/* 变量 起始 结束 for：用 mira_vars 存循环变量，可修改 */
		int slot = prog_var_slot(g_prog, var, var_len);
		if (slot < 0) slot = 0;
		emit("  mov qword [rel mira_vars + %d*8], %lld\n", slot, (long long)start);
		emit("  jmp Lskip_%d\n", L);
		emit("Lblock_%d:\n", L);
		emit("  push rbp\n");
		emit("  mov rbp, rsp\n");
		gen_ops(body && body->kind == OP_BLOCK ? body->u.block : body);
		emit("  pop rbp\n");
		emit("  ret\n");
		emit("Lskip_%d:\n", L);
		pop_loop_context();
		emit("  mov r14, %lld\n", (long long)end);
		emit("Lloop_%d:\n", Lloop);
		emit("  mov rbx, [rel mira_vars + %d*8]\n", slot);
		emit("  cmp rbx, r14\n");
		emit("  jge Lend_%d\n", Lend);
		emit("  push rdx\n");
		emit("  lea rdx, [rel Lblock_%d]\n", L);
		emit("  sub rsp, 32\n");
		emit("  call rdx\n");
		emit("  add rsp, 32\n");
		emit("  pop rdx\n");
		emit("  inc qword [rel mira_vars + %d*8]\n", slot);
		emit("  jmp Lloop_%d\n", Lloop);
		emit("Lend_%d:\n", Lend);
	} else {
		/* 起始 结束 for：索引压栈，body 内不可直接修改 */
		emit("  jmp Lskip_%d\n", L);
		emit("Lblock_%d:\n", L);
		emit("  push rbp\n");
		emit("  mov rbp, rsp\n");
		gen_ops(body && body->kind == OP_BLOCK ? body->u.block : body);
		emit("  pop rbp\n");
		emit("  ret\n");
		emit("Lskip_%d:\n", L);
		pop_loop_context();
		emit("  mov rbx, %lld\n", (long long)start);
		emit("  mov r14, %lld\n", (long long)end);
		emit("Lloop_%d:\n", Lloop);
		emit("  cmp rbx, r14\n");
		emit("  jge Lend_%d\n", Lend);
		emit("  mov [r12], rbx\n");
		emit("  add r12, 8\n");
		stack_depth++;
		type_stack[type_depth++] = TOS_INT;
		emit("  push rdx\n");
		emit("  lea rdx, [rel Lblock_%d]\n", L);
		emit("  sub rsp, 32\n");
		emit("  call rdx\n");
		emit("  add rsp, 32\n");
		emit("  pop rdx\n");
		emit("  sub r12, 8\n");
		stack_depth--;
		if (type_depth > 0) type_depth--;
		emit("  inc rbx\n");
		emit("  jmp Lloop_%d\n", Lloop);
		emit("Lend_%d:\n", Lend);
	}
}

/* { init } { cond } { step } { body } for：类C风格 */
void gen_op_for_cstyle(Op *o) {
	Op *init = o->u.for_cstyle.init;
	Op *cond = o->u.for_cstyle.cond;
	Op *step = o->u.for_cstyle.step;
	Op *body = o->u.for_cstyle.body;
	int Linit = new_label(), Lcond = new_label(), Lstep = new_label(), Lbody = new_label();
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	emit("  jmp Lskip_%d\n", Linit);
	emit("Lblock_%d:\n", Linit);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(init && init->kind == OP_BLOCK ? init->u.block : init);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Linit);
	emit("  jmp Lskip_%d\n", Lcond);
	emit("Lblock_%d:\n", Lcond);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(cond && cond->kind == OP_BLOCK ? cond->u.block : cond);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Lcond);
	emit("  jmp Lskip_%d\n", Lstep);
	emit("Lblock_%d:\n", Lstep);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(step && step->kind == OP_BLOCK ? step->u.block : step);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Lstep);
	emit("  jmp Lskip_%d\n", Lbody);
	emit("Lblock_%d:\n", Lbody);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(body && body->kind == OP_BLOCK ? body->u.block : body);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Lbody);
	pop_loop_context();
	emit("  push rdx\n");
	emit("  lea rdx, [rel Lblock_%d]\n", Linit);
	emit("  sub rsp, 32\n");
	emit("  call rdx\n");
	emit("  add rsp, 32\n");
	emit("  pop rdx\n");
	emit("Lloop_%d:\n", Lloop);
	emit("  push rbx\n");
	emit("  push rsi\n");
	emit("  lea rsi, [rel Lblock_%d]\n", Lcond);
	emit("  sub rsp, 32\n");
	emit("  call rsi\n");
	emit("  add rsp, 32\n");
	emit("  pop rsi\n");
	emit("  pop rbx\n");
	emit("  sub r12, 8\n");
	stack_depth--;
	if (type_depth > 0) type_depth--;
	emit("  mov rax, [r12]\n");
	emit("  test rax, rax\n");
	emit("  jz Lend_%d\n", Lend);
	emit("  push rsi\n");
	emit("  lea rsi, [rel Lblock_%d]\n", Lbody);
	emit("  sub rsp, 32\n");
	emit("  call rsi\n");
	emit("  add rsp, 32\n");
	emit("  pop rsi\n");
	emit("  push rsi\n");
	emit("  lea rsi, [rel Lblock_%d]\n", Lstep);
	emit("  sub rsp, 32\n");
	emit("  call rsi\n");
	emit("  add rsp, 32\n");
	emit("  pop rsi\n");
	emit("  jmp Lloop_%d\n", Lloop);
	emit("Lend_%d:\n", Lend);
}

/* { cond } { body } while */
void gen_op_while_cond(Op *o) {
	Op *cond = o->u.while_cond.cond;
	Op *body = o->u.while_cond.body;
	int Lcond = new_label(), Lbody = new_label();
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	emit("  jmp Lskip_%d\n", Lcond);
	emit("Lblock_%d:\n", Lcond);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(cond->kind == OP_BLOCK ? cond->u.block : cond);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Lcond);
	emit("  jmp Lskip_%d\n", Lbody);
	emit("Lblock_%d:\n", Lbody);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(body->kind == OP_BLOCK ? body->u.block : body);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", Lbody);
	pop_loop_context();
	emit("Lloop_%d:\n", Lloop);
	emit("  push rbx\n");
	emit("  push rsi\n");
	emit("  lea rsi, [rel Lblock_%d]\n", Lcond);
	emit("  sub rsp, 32\n");
	emit("  call rsi\n");
	emit("  add rsp, 32\n");
	emit("  pop rsi\n");
	emit("  pop rbx\n");
	emit("  sub r12, 8\n");
	stack_depth--;
	if (type_depth > 0) type_depth--;
	emit("  mov rax, [r12]\n");
	emit("  test rax, rax\n");
	emit("  jz Lend_%d\n", Lend);
	emit("  push rsi\n");
	emit("  lea rsi, [rel Lblock_%d]\n", Lbody);
	emit("  sub rsp, 32\n");
	emit("  call rsi\n");
	emit("  add rsp, 32\n");
	emit("  pop rsi\n");
	emit("  jmp Lloop_%d\n", Lloop);
	emit("Lend_%d:\n", Lend);
}

/* start end step for var ... loop */
void gen_op_for_ext(Op *o) {
	int64_t start = o->u.for_ext.start;
	int64_t end = o->u.for_ext.end;
	int64_t step = o->u.for_ext.step;
	char *var = o->u.for_ext.var;
	size_t var_len = o->u.for_ext.var_len;
	Op *body = o->u.for_ext.body;

	int slot = prog_var_slot(g_prog, var, var_len);
	if (slot < 0) slot = 0;

	int L = new_label();
	emit("  mov qword [rel mira_vars + %d*8], %lld\n", slot, (long long)start);
	emit("  jmp Lskip_%d\n", L);
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	emit("Lblock_%d:\n", L);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(body);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", L);
	pop_loop_context();

	emit("  mov r14, %lld\n", (long long)end);  /* r14=end，callee-saved 不会被 C 破坏 */
	emit("Lloop_%d:\n", Lloop);
	emit("  mov rbx, [rel mira_vars + %d*8]\n", slot);
	emit("  cmp rbx, r14\n");
	if (step > 0)
		emit("  jge Lend_%d\n", Lend);
	else
		emit("  jle Lend_%d\n", Lend);
	emit("  push rdx\n");
	emit("  lea rdx, [rel Lblock_%d]\n", L);
	emit("  sub rsp, 32\n");
	emit("  call rdx\n");
	emit("  add rsp, 32\n");
	emit("  pop rdx\n");
	emit("  add qword [rel mira_vars + %d*8], %lld\n", slot, (long long)step);
	emit("  jmp Lloop_%d\n", Lloop);
	emit("Lend_%d:\n", Lend);
}

/* 列表 each { body } */
void gen_op_each(Op *o) {
	Op *list_op = o->u.each.list;
	Op *body = o->u.each.body;
	gen_op(list_op, NULL);  /* 推送列表到栈 */
	/* 此时列表在栈顶，弹出并迭代 */
	int L = new_label();
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	emit("  sub r12, 8\n");
	emit("  mov r15, [r12]\n");  /* r15 = list ptr */
	stack_depth--;
	if (type_depth > 0) type_depth--;
	emit("  mov rcx, r15\n");
	emit("  sub rsp, 32\n");
	emit("  call mira_list_len\n");
	emit("  add rsp, 32\n");
	emit("  mov r14, rax\n");  /* r14 = len */
	emit("  xor rbx, rbx\n");  /* rbx = i */
	emit("  jmp Lcond_%d\n", L);
	emit("Lblock_%d:\n", L);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(body && body->kind == OP_BLOCK ? body->u.block : body);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lcond_%d:\n", L);
	emit("  cmp rbx, r14\n");
	emit("  jge Lend_%d\n", Lend);
	emit("  mov rcx, r15\n");
	emit("  mov rdx, rbx\n");
	emit("  sub rsp, 32\n");
	emit("  call mira_list_get\n");
	emit("  add rsp, 32\n");
	emit("  mov [r12], rax\n");
	emit("  add r12, 8\n");
	stack_depth++;
	type_stack[type_depth++] = TOS_INT;
	emit("  push rdx\n");
	emit("  lea rdx, [rel Lblock_%d]\n", L);
	emit("  sub rsp, 32\n");
	emit("  call rdx\n");
	emit("  add rsp, 32\n");
	emit("  pop rdx\n");
	emit("  sub r12, 8\n");
	stack_depth--;
	if (type_depth > 0) type_depth--;
	emit("  inc rbx\n");
	emit("  jmp Lcond_%d\n", L);
	emit("Lend_%d:\n", Lend);
	pop_loop_context();
}

/* while ... loop 无限循环 */
void gen_op_while_inf(Op *o) {
	Op *body = o->u.while_inf.body;
	int L = new_label();
	int Lloop = new_label(), Lend = new_label();
	push_loop_context(Lend, Lloop);
	emit("  jmp Lskip_%d\n", L);
	emit("Lblock_%d:\n", L);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(body);
	emit("  pop rbp\n");
	emit("  ret\n");
	emit("Lskip_%d:\n", L);
	pop_loop_context();
	emit("Lloop_%d:\n", Lloop);
	emit("  push rdx\n");
	emit("  lea rdx, [rel Lblock_%d]\n", L);
	emit("  sub rsp, 32\n");
	emit("  call rdx\n");
	emit("  add rsp, 32\n");
	emit("  pop rdx\n");
	emit("  jmp Lloop_%d\n", Lloop);
	emit("Lend_%d:\n", Lend);
}
