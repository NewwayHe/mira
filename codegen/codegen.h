/* codegen 各模块共享：状态、TOS 常量 */
#ifndef CODEGEN_CODEGEN_H
#define CODEGEN_CODEGEN_H

#include "mira.h"
#include <stddef.h>

/* 栈顶类型：0=int, 1=string(ptr), 2=float, 3=bool */
#define TOS_INT    0
#define TOS_STR    1
#define TOS_FLOAT  2
#define TOS_BOOL   3

/* 全局状态（在 state.c 中定义） */
extern Compiler *comp;
extern Program *g_prog;
extern Def *current_def;
extern int stack_depth;
extern int type_stack[512];
extern int type_depth;
extern int var_types[256];
extern int last_var_slot;

/* emit 与标签（emit.c） */
void emit(const char *fmt, ...);
void emit_push_rax(void);
void emit_pop_rax(void);
void emit_push_imm(int64_t v);
int new_label(void);
int param_slot(const char *name, size_t len);
int prog_var_slot(Program *prog, const char *name, size_t len);

void push_loop_context(int Lend, int Lcontinue);
void pop_loop_context(void);
int get_loop_end(void);
int get_loop_continue(void);

void push_switch_context(int Lend, int Lcontinue, int cont_kind);
void pop_switch_context(void);
int get_switch_end(void);
int get_switch_continue(void);
int get_switch_continue_kind(void);

void gen_ops(Op *list);
void gen_op(Op *o, Op *next);

#endif
