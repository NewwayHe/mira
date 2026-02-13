/* codegen 全局状态 */
#include "codegen.h"

Compiler *comp;
Program *g_prog;
Def *current_def;
int stack_depth;
int type_stack[512];
int type_depth;
int var_types[256];
int last_var_slot;

/* break/continue：当前循环的 Lend、Lloop 标签 */
static int loop_end_stack[8], loop_continue_stack[8];
static int loop_depth;

void push_loop_context(int Lend, int Lcontinue) {
	if (loop_depth < 8) {
		loop_end_stack[loop_depth] = Lend;
		loop_continue_stack[loop_depth] = Lcontinue;
		loop_depth++;
	}
}
void pop_loop_context(void) { if (loop_depth > 0) loop_depth--; }
int get_loop_end(void) { return loop_depth > 0 ? loop_end_stack[loop_depth - 1] : -1; }
int get_loop_continue(void) { return loop_depth > 0 ? loop_continue_stack[loop_depth - 1] : -1; }

/* switch 内 break/continue：break 跳到 switch 结束，continue 跳到下一 case */
/* continue_target: 0=case(Lsw_X), 1=default(Lsw_def_X), 2=end(Lsw_end_X) */
static int switch_end_stack[4], switch_continue_stack[4], switch_continue_kind[4];
static int switch_depth;

void push_switch_context(int Lend, int Lcontinue, int cont_kind) {
	if (switch_depth < 4) {
		switch_end_stack[switch_depth] = Lend;
		switch_continue_stack[switch_depth] = Lcontinue;
		switch_continue_kind[switch_depth] = cont_kind;
		switch_depth++;
	}
}
void pop_switch_context(void) { if (switch_depth > 0) switch_depth--; }
int get_switch_end(void) { return switch_depth > 0 ? switch_end_stack[switch_depth - 1] : -1; }
int get_switch_continue(void) { return switch_depth > 0 ? switch_continue_stack[switch_depth - 1] : -1; }
int get_switch_continue_kind(void) { return switch_depth > 0 ? switch_continue_kind[switch_depth - 1] : -1; }
