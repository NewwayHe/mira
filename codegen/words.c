/* 内建词：OP_WORD 全部分支；OP_IF 内联生成 */
#include "codegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* OP_SWITCH：值 模式1 { 代码1 } 模式2 { 代码2 } 默认 { 默认代码 } switch */
void gen_op_switch(Op *o) {
	Op *value = o->u.switch_.value;
	Op *cases = o->u.switch_.cases;
	Op *default_block = o->u.switch_.default_block;
	gen_ops(value);
	if (type_depth > 0) type_depth--;
	emit_pop_rax();
	stack_depth--;
	int Lend = new_label();
	int Ldef = new_label();
	int n = 0;
	for (Op *p = cases; p && p->next; p = p->next->next) n++;
	int *Larr = n > 0 ? malloc((size_t)n * sizeof(int)) : NULL;
	int i = 0;
	for (Op *p = cases; p && p->next; p = p->next->next, i++) {
		Larr[i] = new_label();
		Op *pat = p;
		int64_t imm = 0;
		int is_int = 0;
		if (pat->kind == OP_INT) { imm = pat->u.i; is_int = 1; }
		else if (pat->kind == OP_CONST && g_prog && g_prog->const_kinds[pat->u.const_slot] == CONST_INT) {
			imm = g_prog->const_ints[pat->u.const_slot];
			is_int = 1;
		}
		if (is_int) {
			emit("  cmp rax, %lld\n", (long long)imm);
			emit("  je Lsw_%d\n", Larr[i]);
		} else {
			emit_push_rax();
			gen_op(pat, NULL);
			if (type_depth > 0) type_depth--;
			emit("  sub r12, 8\n");
			emit("  mov rcx, [r12]\n");
			emit("  sub r12, 8\n");
			emit("  mov rax, [r12]\n");
			stack_depth -= 2;
			emit("  cmp rax, rcx\n");
			emit("  je Lsw_%d\n", Larr[i]);
		}
	}
	if (default_block)
		emit("  jmp Lsw_def_%d\n", Ldef);
	else
		emit("  jmp Lsw_end_%d\n", Lend);
	i = 0;
	for (Op *p = cases; p && p->next; p = p->next->next, i++) {
		Op *blk = p->next;
		int next_label = (i + 1 < n) ? Larr[i + 1] : (default_block ? Ldef : Lend);
		int next_kind = (i + 1 < n) ? 0 : (default_block ? 1 : 2);
		push_switch_context(Lend, next_label, next_kind);
		emit("Lsw_%d:\n", Larr[i]);
		gen_ops(blk && blk->kind == OP_BLOCK ? blk->u.block : blk);
		pop_switch_context();
		emit("  jmp Lsw_end_%d\n", Lend);  /* 每个 case 后默认 break */
	}
	if (default_block) {
		push_switch_context(Lend, Lend, 2);
		emit("Lsw_def_%d:\n", Ldef);
		gen_ops(default_block->kind == OP_BLOCK ? default_block->u.block : default_block);
		pop_switch_context();
	}
	emit("Lsw_end_%d:\n", Lend);
	free(Larr);
}

/* OP_IF：内联 cond、then、else，避免 call/ret 导致的崩溃 */
void gen_op_if(Op *o) {
	Op *cond = o->u.iff.cond;
	Op *then_b = o->u.iff.then_b;
	Op *else_b = o->u.iff.else_b;
	gen_ops(cond);
	if (type_depth > 0) type_depth--;
	emit_pop_rax();
	stack_depth--;
	int Lelse = new_label(), Lend = new_label();
	emit("  test rax, rax\n");
	emit("  jz Lif_%d\n", Lelse);
	gen_ops(then_b && then_b->kind == OP_BLOCK ? then_b->u.block : then_b);
	emit("  jmp Lif_%d\n", Lend);
	emit("Lif_%d:\n", Lelse);
	if (else_b)
		gen_ops(else_b->kind == OP_BLOCK ? else_b->u.block : else_b);
	emit("Lif_%d:\n", Lend);
}

void gen_op_word(Op *o) {
	const char *name = o->u.word.name;
	size_t len = o->u.word.len;
	int p = param_slot(name, len);
	if (p >= 0 && current_def) {
		if (p == 0) emit("  mov rax, rcx\n");
		else if (p == 1) emit("  mov rax, rdx\n");
		else if (p == 2) emit("  mov rax, r8\n");
		else if (p == 3) emit("  mov rax, r9\n");
		else emit("  mov rax, [rbp+%d]\n", 16 + (p - 4) * 8);
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 4 && memcmp(name, "true", 4) == 0) {
		emit_push_imm(1);
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 5 && memcmp(name, "false", 5) == 0) {
		emit_push_imm(0);
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	/* 调试 */
	if (len == 2 && name[0] == '.' && name[1] == 's') {
		emit("  lea rcx, [rel mira_stack]\n");
		emit("  mov rdx, r12\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dump_data_stack\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 2 && name[0] == '.' && name[1] == 'r') {
		emit("  sub rsp, 32\n");
		emit("  call mira_dump_return_stack\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 2 && name[0] == '.' && name[1] == 't') {
		emit("  sub rsp, 32\n");
		emit("  call mira_dump_type_stack\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, ".var", 4) == 0) {
		int slot = last_var_slot;
		if (slot < 0) slot = 0;
		emit("  mov ecx, %d\n", slot);
		emit("  sub rsp, 32\n");
		emit("  call mira_dump_var_slot\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 5 && memcmp(name, ".vars", 5) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_dump_vars\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 6 && memcmp(name, ".stats", 6) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_stats\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 6 && memcmp(name, ".words", 6) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_backtrace\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 10 && memcmp(name, ".backtrace", 10) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_backtrace\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 6 && memcmp(name, ".where", 6) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_where\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 5 && memcmp(name, "break", 5) == 0) {
		int Lend = get_loop_end();
		if (Lend >= 0) {
			emit("  jmp Lend_%d\n", Lend);
		} else {
			emit("  sub rsp, 32\n");
			emit("  call mira_debug_break\n");
			emit("  add rsp, 32\n");
		}
		return;
	}
	if (len == 10 && memcmp(name, "breakpoint", 10) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_debug_break\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, "step", 4) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_debug_step\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, "next", 4) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_debug_next\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 8 && memcmp(name, "continue", 8) == 0) {
		int Lcont = get_switch_continue();
		int k = get_switch_continue_kind();
		if (Lcont >= 0 && k >= 0) {
			if (k == 0) emit("  jmp Lsw_%d\n", Lcont);
			else if (k == 1) emit("  jmp Lsw_def_%d\n", Lcont);
			else emit("  jmp Lsw_end_%d\n", Lcont);
		} else {
			Lcont = get_loop_continue();
			if (Lcont >= 0) {
				emit("  jmp Lloop_%d\n", Lcont);
			} else {
				emit("  sub rsp, 32\n");
				emit("  call mira_debug_continue\n");
				emit("  add rsp, 32\n");
			}
		}
		return;
	}
	if (len == 5 && memcmp(name, "watch", 5) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_watch_not_supported\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* 算术：+ - * / 支持 int 与 float，含 int+float 自动提升 */
	{
		int t1 = type_depth >= 2 ? type_stack[type_depth - 2] : TOS_INT;
		int t2 = type_depth >= 1 ? type_stack[type_depth - 1] : TOS_INT;
		int both_int = (t1 == TOS_INT && t2 == TOS_INT);
		if (len == 1 && *name == '+') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  add rax, rcx\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_INT;
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  addsd xmm0, xmm1\n");
				emit("  movq rax, xmm0\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_FLOAT;
			}
			return;
		}
		if (len == 1 && *name == '-') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  sub rax, rcx\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_INT;
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  subsd xmm0, xmm1\n");
				emit("  movq rax, xmm0\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_FLOAT;
			}
			return;
		}
		if (len == 1 && *name == '*') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  imul rax, rcx\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_INT;
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  mulsd xmm0, xmm1\n");
				emit("  movq rax, xmm0\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_FLOAT;
			}
			return;
		}
		if (len == 1 && *name == '/') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  xor rdx, rdx\n");
				emit("  idiv rcx\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_INT;
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  divsd xmm0, xmm1\n");
				emit("  movq rax, xmm0\n");
				emit_push_rax();
				type_stack[type_depth++] = TOS_FLOAT;
			}
			return;
		}
	}
	if (len == 2 && name[0] == 'f' && name[1] == '+') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  movq xmm1, rax\n");
		emit_pop_rax();
		emit("  movq xmm0, rax\n");
		emit("  addsd xmm0, xmm1\n");
		emit("  movq rax, xmm0\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	if (len == 2 && name[0] == 'f' && name[1] == '-') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  movq xmm1, rax\n");
		emit_pop_rax();
		emit("  movq xmm0, rax\n");
		emit("  subsd xmm0, xmm1\n");
		emit("  movq rax, xmm0\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	if (len == 2 && name[0] == 'f' && name[1] == '*') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  movq xmm1, rax\n");
		emit_pop_rax();
		emit("  movq xmm0, rax\n");
		emit("  mulsd xmm0, xmm1\n");
		emit("  movq rax, xmm0\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	if (len == 2 && name[0] == 'f' && name[1] == '/') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  movq xmm1, rax\n");
		emit_pop_rax();
		emit("  movq xmm0, rax\n");
		emit("  divsd xmm0, xmm1\n");
		emit("  movq rax, xmm0\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	/* 比较与逻辑：= != 支持 int 与 float */
	{
		int t1 = type_depth >= 2 ? type_stack[type_depth - 2] : TOS_INT;
		int t2 = type_depth >= 1 ? type_stack[type_depth - 1] : TOS_INT;
		int both_int = (t1 == TOS_INT && t2 == TOS_INT);
		if (len == 1 && *name == '=') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  cmp rax, rcx\n");
				emit("  sete al\n");
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  ucomisd xmm0, xmm1\n");
				emit("  setz al\n");
				emit("  setnp cl\n");
				emit("  and al, cl\n");
			}
			emit("  movzx eax, al\n");
			emit_push_rax();
			type_stack[type_depth++] = TOS_BOOL;
			return;
		}
		if (len == 2 && name[0] == '!' && name[1] == '=') {
			type_depth -= 2;
			emit_pop_rax();
			emit("  mov rcx, rax\n");
			emit_pop_rax();
			if (both_int) {
				emit("  cmp rax, rcx\n");
				emit("  setne al\n");
			} else {
				if (t2 == TOS_INT) emit("  cvtsi2sd xmm1, rcx\n");
				else emit("  movq xmm1, rcx\n");
				if (t1 == TOS_INT) emit("  cvtsi2sd xmm0, rax\n");
				else emit("  movq xmm0, rax\n");
				emit("  ucomisd xmm0, xmm1\n");
				emit("  setz al\n");
				emit("  setnp cl\n");
				emit("  and al, cl\n");
				emit("  xor al, 1\n");
			}
			emit("  movzx eax, al\n");
			emit_push_rax();
			type_stack[type_depth++] = TOS_BOOL;
			return;
		}
	}
	if (len == 3 && memcmp(name, "and", 3) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, 0\n");
		emit("  setne al\n");
		emit("  movzx eax, al\n");
		emit("  cmp rcx, 0\n");
		emit("  setne cl\n");
		emit("  and al, cl\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 2 && memcmp(name, "or", 2) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, 0\n");
		emit("  setne al\n");
		emit("  movzx eax, al\n");
		emit("  cmp rcx, 0\n");
		emit("  setne cl\n");
		emit("  or al, cl\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 3 && memcmp(name, "xor", 3) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, 0\n");
		emit("  setne al\n");
		emit("  movzx eax, al\n");
		emit("  cmp rcx, 0\n");
		emit("  setne cl\n");
		emit("  xor al, cl\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 3 && memcmp(name, "not", 3) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  cmp rax, 0\n");
		emit("  sete al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 2 && name[0] == '!' && name[1] == '=') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, rcx\n");
		emit("  setne al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 1 && *name == '<') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, rcx\n");
		emit("  setl al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 1 && *name == '>') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, rcx\n");
		emit("  setg al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 2 && name[0] == '<' && name[1] == '=') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, rcx\n");
		emit("  setle al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	if (len == 2 && name[0] == '>' && name[1] == '=') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  cmp rax, rcx\n");
		emit("  setge al\n");
		emit("  movzx eax, al\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_BOOL;
		return;
	}
	/* @ ! c! c@ */
	if (len == 1 && *name == '@') {
		int t = type_depth > 0 ? type_stack[type_depth - 1] : TOS_INT;
		type_depth--;
		emit_pop_rax();
		emit("  mov rax, [rax]\n");
		emit_push_rax();
		type_stack[type_depth++] = t;
		return;
	}
	if (len == 1 && *name == '!') {
		int addr_type = type_stack[--type_depth];
		int value_type = type_stack[--type_depth];
		(void)addr_type;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		if (last_var_slot >= 0 && last_var_slot < 256)
			var_types[last_var_slot] = value_type;
		emit_pop_rax();
		emit("  mov [rcx], rax\n");
		emit_push_rax();
		type_stack[type_depth++] = value_type;
		return;
	}
	if (len == 2 && name[0] == 'c' && name[1] == '!') {
		(void)type_stack[--type_depth];
		int value_type = type_stack[--type_depth];
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  mov byte [rcx], al\n");
		emit_push_rax();
		type_stack[type_depth++] = value_type;
		return;
	}
	if (len == 2 && name[0] == 'c' && name[1] == '@') {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  movzx eax, byte [rax]\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	/* . 等价于 print */
	if (len == 1 && *name == '.') {
		int t = type_depth > 0 ? type_stack[--type_depth] : TOS_INT;
		emit_pop_rax();
		if (t == TOS_FLOAT) {
			emit("  mov [rel mira_float_tmp], rax\n");
			emit("  mov ecx, 2\n");
			emit("  lea rdx, [rel mira_float_tmp]\n");
		} else {
			emit("  mov ecx, %d\n", t);
			emit("  mov rdx, rax\n");
		}
		emit("  sub rsp, 32\n");
		emit("  call mira_print\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* print read */
	if (len == 5 && memcmp(name, "print", 5) == 0) {
		int t = type_depth > 0 ? type_stack[--type_depth] : TOS_INT;
		emit_pop_rax();
		if (t == TOS_FLOAT) {
			emit("  mov [rel mira_float_tmp], rax\n");
			emit("  mov ecx, 2\n");
			emit("  lea rdx, [rel mira_float_tmp]\n");
		} else {
			emit("  mov ecx, %d\n", t);
			emit("  mov rdx, rax\n");
		}
		emit("  sub rsp, 32\n");
		emit("  call mira_print\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, "read", 4) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_read_int\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 5 && memcmp(name, "input", 5) == 0) {
		emit("  sub rsp, 32\n");
		emit("  call mira_input\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_STR;
		return;
	}
	/* allocate free */
	if (len == 8 && memcmp(name, "allocate", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mem_alloc\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 4 && memcmp(name, "free", 4) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mem_free\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* list-* */
	if (len == 8 && memcmp(name, "list-new", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_new\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "list-len", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_len\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "list-get", 8) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_get\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "list-set", 8) == 0) {
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
		return;
	}
	if (len == 9 && memcmp(name, "list-free", 9) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_free\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* dict-* */
	if (len == 8 && memcmp(name, "dict-new", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_new\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "dict-set", 8) == 0) {
		type_depth -= 3;
		emit_pop_rax();
		emit("  mov r8, rax\n");
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_set\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 8 && memcmp(name, "dict-get", 8) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_get\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "dict-has", 8) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_has\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 9 && memcmp(name, "dict-free", 9) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_free\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* str-* 字符串操作 */
	if (len == 7 && memcmp(name, "str-len", 7) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_str_len\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 10 && memcmp(name, "str-concat", 10) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_str_concat\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_STR;
		return;
	}
	if (len == 8 && memcmp(name, "str-copy", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_str_copy\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_STR;
		return;
	}
	/* 类型转换 */
	if (len == 8 && memcmp(name, "int->str", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_int_to_str\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_STR;
		return;
	}
	if (len == 8 && memcmp(name, "str->int", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_str_to_int\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 10 && memcmp(name, "int->float", 10) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_int_to_float\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_FLOAT;
		return;
	}
	if (len == 10 && memcmp(name, "float->int", 10) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_float_to_int\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	/* list-push list-pop */
	if (len == 9 && memcmp(name, "list-push", 9) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_push\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 8 && memcmp(name, "list-pop", 8) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_list_pop\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	/* dict-keys dict-count */
	if (len == 9 && memcmp(name, "dict-keys", 9) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_keys\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 10 && memcmp(name, "dict-count", 10) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_dict_count\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	/* file-* 文件操作 */
	if (len == 9 && memcmp(name, "file-read", 9) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_file_read\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_STR;
		return;
	}
	if (len == 10 && memcmp(name, "file-write", 10) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_file_write\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 11 && memcmp(name, "file-append", 11) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_file_append\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	if (len == 11 && memcmp(name, "file-exists", 11) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_file_exists\n");
		emit("  add rsp, 32\n");
		emit_push_rax();
		type_stack[type_depth++] = TOS_INT;
		return;
	}
	/* move erase dump md */
	if (len == 4 && memcmp(name, "move", 4) == 0) {
		type_depth -= 3;
		emit_pop_rax();
		emit("  mov r8, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mem_move\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 5 && memcmp(name, "erase", 5) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mem_erase\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, "dump", 4) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  mov rdx, 16\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_mem_dump\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 2 && name[0] == 'm' && name[1] == 'd') {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call mira_mem_dump\n");
		emit("  add rsp, 32\n");
		return;
	}
	if (len == 4 && memcmp(name, "exit", 4) == 0) {
		if (type_depth > 0) type_depth--;
		emit_pop_rax();
		emit("  mov rcx, rax\n");
		emit("  sub rsp, 32\n");
		emit("  call ExitProcess\n");
		emit("  add rsp, 32\n");
		return;
	}
	/* return：提前从当前函数返回，栈顶值即为返回值（若有） */
	if (len == 6 && memcmp(name, "return", 6) == 0) {
		emit("  pop rbp\n");
		emit("  ret\n");
		return;
	}
	/* while：{ condition } { body } while，条件块每次迭代求值，为 0 则退出 */
	if (len == 5 && memcmp(name, "while", 5) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rbx, rax\n");   /* rbx = body block */
		emit_pop_rax();
		emit("  mov rsi, rax\n");   /* rsi = cond block */
		stack_depth -= 2;
		int Lloop = new_label(), Lend = new_label();
		emit("Lloop_%d:\n", Lloop);
		emit("  push rbx\n");       /* 保存 rbx，call 会破坏 */
		emit("  sub rsp, 32\n");
		emit("  call rsi\n");       /* call cond */
		emit("  add rsp, 32\n");
		emit("  pop rbx\n");
		emit_pop_rax();
		emit("  test rax, rax\n");
		emit("  jz Lend_%d\n", Lend);
		emit("  push rsi\n");       /* 保存 rsi，call 会破坏 */
		emit("  sub rsp, 32\n");
		emit("  call rbx\n");       /* call body */
		emit("  add rsp, 32\n");
		emit("  pop rsi\n");
		emit("  jmp Lloop_%d\n", Lloop);
		emit("Lend_%d:\n", Lend);
		return;
	}
	/* for：n { body } for，执行 n 次，每次迭代栈顶为当前索引（0..n-1） */
	if (len == 3 && memcmp(name, "for", 3) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");   /* rdx = body block */
		emit_pop_rax();
		emit("  mov rcx, rax\n");   /* rcx = n */
		stack_depth -= 2;
		int Lloop = new_label(), Lend = new_label();
		emit("  xor rbx, rbx\n");   /* rbx = i = 0 */
		emit("Lloop_%d:\n", Lloop);
		emit("  cmp rbx, rcx\n");
		emit("  jge Lend_%d\n", Lend);
		emit("  mov [r12], rbx\n");
		emit("  add r12, 8\n");
		stack_depth++;
		type_stack[type_depth++] = TOS_INT;
		emit("  push rdx\n");       /* 保存 rdx，call 会破坏 */
		emit("  sub rsp, 32\n");
		emit("  call rdx\n");
		emit("  add rsp, 32\n");
		emit("  pop rdx\n");
		stack_depth--;
		if (type_depth > 0) type_depth--;
		emit("  inc rbx\n");
		emit("  jmp Lloop_%d\n", Lloop);
		emit("Lend_%d:\n", Lend);
		return;
	}
	/* when */
	if (len == 4 && memcmp(name, "when", 4) == 0) {
		type_depth -= 2;
		emit_pop_rax();
		emit("  mov rdx, rax\n");
		emit_pop_rax();
		stack_depth -= 2;
		int Lend = new_label();
		emit("  test rax, rax\n");
		emit("  jz Lwhen_%d\n", Lend);
		emit("  sub rsp, 40\n");  /* 32 shadow + 8 对齐 */
		emit("  call rdx\n");
		emit("  add rsp, 40\n");
		emit("Lwhen_%d:\n", Lend);
		return;
	}
	if (len == 2 && memcmp(name, "if", 2) == 0) {
		fprintf(stderr, "internal: if should be OP_IF\n");
		exit(1);
	}
	if (len == 6 && memcmp(name, "switch", 6) == 0) {
		fprintf(stderr, "internal: switch should be OP_SWITCH\n");
		exit(1);
	}
	/* 用户词 */
	{
		Def *d = g_prog ? g_prog->defs : NULL;
		for (; d; d = d->next)
			if (d->name_len == len && memcmp(d->name, name, len) == 0)
				break;
		if (d && d->param_count > 0) {
			type_depth -= d->param_count;
			for (int i = d->param_count - 1; i >= 0; i--) {
				emit_pop_rax();
				if (i == 0) emit("  mov rcx, rax\n");
				else if (i == 1) emit("  mov rdx, rax\n");
				else if (i == 2) emit("  mov r8, rax\n");
				else if (i == 3) emit("  mov r9, rax\n");
				else { emit("  push rax\n"); }
			}
			emit("  sub rsp, 32\n");
			emit("  call %.*s\n", (int)len, name);
			emit("  add rsp, 32\n");
			stack_depth -= d->param_count;
			stack_depth++;
			type_stack[type_depth++] = TOS_INT;
			return;
		}
		if (d) {
			emit("  sub rsp, 32\n");
			emit("  call %.*s\n", (int)len, name);
			emit("  add rsp, 32\n");
			stack_depth++;
			type_stack[type_depth++] = TOS_INT;
			return;
		}
	}
	fprintf(stderr, "unknown word: %.*s\n", (int)len, name);
	exit(1);
}
