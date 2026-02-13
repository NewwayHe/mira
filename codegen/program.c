/* 顶层：gen_def、codegen 入口 */
#include "codegen.h"
#include <stdio.h>

void gen_def(Def *d) {
	current_def = d;
	stack_depth = 0;
	type_depth = 0;
	last_var_slot = -1;
	emit("%.*s:\n", (int)d->name_len, d->name);
	emit("  push rbp\n");
	emit("  mov rbp, rsp\n");
	gen_ops(d->body);
	emit("  pop rbp\n");
	emit("  ret\n\n");
	current_def = NULL;
}

void codegen(Compiler *c, Program *prog) {
	comp = c;
	g_prog = prog;
	c->label_id = 0;
	fprintf(comp->out, ";; Mira – 栈式 → x64 Win\n");
	fprintf(comp->out, "default rel\n");
	fflush(comp->out);
	fprintf(comp->out, "section .data\n");
	for (int i = 0; i < prog->const_count; i++) {
		if (prog->const_kinds[i] == CONST_DOUBLE)
			fprintf(comp->out, "const_dbl.%d: dq %.17e\n", i, prog->const_doubles[i]);
		else if (prog->const_kinds[i] == CONST_STR) {
			fprintf(comp->out, "const_str.%d: db ", i);
			const char *s = prog->const_strs[i];
			size_t len = prog->const_str_lens[i];
			for (size_t j = 0; j < len; j++)
				fprintf(comp->out, "%u%s", (unsigned)(unsigned char)s[j], j + 1 < len ? "," : "");
			fprintf(comp->out, ",0\n");
		}
	}
	if (prog->var_count > 0) {
		fprintf(comp->out, "mira_var_count: dq %d\n", prog->var_count);
		for (int i = 0; i < prog->var_count; i++) {
			const char *name = prog->var_names[i];
			size_t len = prog->var_lens[i];
			fprintf(comp->out, "mira_var_name_%d: db ", i);
			for (size_t j = 0; j < len; j++)
				fprintf(comp->out, "%u%s", (unsigned)(unsigned char)name[j], j + 1 < len ? "," : "");
			fprintf(comp->out, ",0\n");
		}
		fprintf(comp->out, "mira_var_names: dq ");
		for (int i = 0; i < prog->var_count; i++)
			fprintf(comp->out, "mira_var_name_%d%s", i, i + 1 < prog->var_count ? "," : "");
		fprintf(comp->out, "\n");
	} else {
		fprintf(comp->out, "mira_var_count: dq 0\n");
		fprintf(comp->out, "mira_var_names: dq 0\n");
	}
	fprintf(comp->out, "section .bss\n");
	fprintf(comp->out, "mira_stack: resq 512\n");
	fprintf(comp->out, "mira_vars: resq %d\n", prog->var_count > 0 ? prog->var_count : 1);
	fprintf(comp->out, "section .text\n");
	fprintf(comp->out, "extern mira_float_tmp\n");
	fprintf(comp->out, "global mira_main\n");
	fprintf(comp->out, "global mira_var_count\n");
	fprintf(comp->out, "global mira_var_names\n");
	fprintf(comp->out, "global mira_vars\n");
	fprintf(comp->out, "extern mira_print\n");
	fprintf(comp->out, "extern mira_read_int\n");
	fprintf(comp->out, "extern mira_input\n");
	fprintf(comp->out, "extern ExitProcess\n");
	fprintf(comp->out, "extern mem_alloc\n");
	fprintf(comp->out, "extern mem_free\n");
	fprintf(comp->out, "extern mem_move\n");
	fprintf(comp->out, "extern mem_erase\n");
	fprintf(comp->out, "extern mira_mem_dump\n");
	fprintf(comp->out, "extern mira_dump_data_stack\n");
	fprintf(comp->out, "extern mira_dump_var_slot\n");
	fprintf(comp->out, "extern mira_dump_vars\n");
	fprintf(comp->out, "extern mira_stats\n");
	fprintf(comp->out, "extern mira_debug_break\n");
	fprintf(comp->out, "extern mira_debug_step\n");
	fprintf(comp->out, "extern mira_debug_next\n");
	fprintf(comp->out, "extern mira_debug_continue\n");
	fprintf(comp->out, "extern mira_dump_return_stack\n");
	fprintf(comp->out, "extern mira_dump_type_stack\n");
	fprintf(comp->out, "extern mira_backtrace\n");
	fprintf(comp->out, "extern mira_where\n");
	fprintf(comp->out, "extern mira_watch_not_supported\n");
	fprintf(comp->out, "extern mira_list_new\n");
	fprintf(comp->out, "extern mira_list_len\n");
	fprintf(comp->out, "extern mira_list_get\n");
	fprintf(comp->out, "extern mira_list_set\n");
	fprintf(comp->out, "extern mira_list_free\n");
	fprintf(comp->out, "extern mira_dict_new\n");
	fprintf(comp->out, "extern mira_dict_set\n");
	fprintf(comp->out, "extern mira_dict_get\n");
	fprintf(comp->out, "extern mira_dict_has\n");
	fprintf(comp->out, "extern mira_dict_free\n");
	fprintf(comp->out, "extern mira_str_len\n");
	fprintf(comp->out, "extern mira_str_concat\n");
	fprintf(comp->out, "extern mira_str_copy\n");
	fprintf(comp->out, "extern mira_int_to_str\n");
	fprintf(comp->out, "extern mira_str_to_int\n");
	fprintf(comp->out, "extern mira_int_to_float\n");
	fprintf(comp->out, "extern mira_float_to_int\n");
	fprintf(comp->out, "extern mira_list_push\n");
	fprintf(comp->out, "extern mira_list_pop\n");
	fprintf(comp->out, "extern mira_dict_keys\n");
	fprintf(comp->out, "extern mira_dict_count\n");
	fprintf(comp->out, "extern mira_file_read\n");
	fprintf(comp->out, "extern mira_file_write\n");
	fprintf(comp->out, "extern mira_file_append\n");
	fprintf(comp->out, "extern mira_file_exists\n\n");
	for (Def *d = prog->defs; d; d = d->next)
		gen_def(d);
	fprintf(comp->out, "mira_main:\n");
	if (prog->main_block) {
		emit("  push rbp\n");
		emit("  mov rbp, rsp\n");
		emit("  push r12\n");
		emit("  sub rsp, 8\n");  /* 重新 16 字节对齐 rsp（push rbp + push r12 = 16 字节，加上返回地址 8 字节 = 24，需要再减 8 才能 %16==0） */
		emit("  lea r12, [rel mira_stack]\n");
		current_def = NULL;
		stack_depth = 0;
		type_depth = 0;
		last_var_slot = -1;
		for (int i = 0; i < 256; i++) var_types[i] = TOS_INT;
		gen_ops(prog->main_block);
		emit("  add rsp, 8\n");  /* 恢复 sub rsp, 8 */
		emit("  pop r12\n");
		emit("  pop rbp\n");
		emit("  xor eax, eax\n");
		emit("  ret\n");
	} else {
		emit("  xor eax, eax\n");
		emit("  ret\n");
	}
}
