/* emit 与栈/标签辅助 */
#include "codegen.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void emit_impl(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(comp->out, fmt, ap);
	va_end(ap);
}

void emit(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(comp->out, fmt, ap);
	va_end(ap);
}

void emit_push_rax(void) {
	emit_impl("  mov [r12], rax\n");
	emit_impl("  add r12, 8\n");
	stack_depth++;
}

void emit_pop_rax(void) {
	emit_impl("  sub r12, 8\n");
	emit_impl("  mov rax, [r12]\n");
	stack_depth--;
}

void emit_push_imm(int64_t v) {
	emit_impl("  mov qword [r12], %lld\n", (long long)v);
	emit_impl("  add r12, 8\n");
	stack_depth++;
}

int new_label(void) {
	return ++comp->label_id;
}

int param_slot(const char *name, size_t len) {
	if (!current_def) return -1;
	for (int i = 0; i < current_def->param_count; i++)
		if (current_def->param_lens[i] == len && memcmp(current_def->params[i], name, len) == 0)
			return i;
	return -1;
}

int prog_var_slot(Program *prog, const char *name, size_t len) {
	if (!prog) return -1;
	for (int i = 0; i < prog->var_count; i++)
		if (prog->var_lens[i] == len && memcmp(prog->var_names[i], name, len) == 0)
			return i;
	return -1;
}
