/* Mira – 栈式/后置语法，按 语言格式.txt 规范 */
#ifndef MIRA_H
#define MIRA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

/* --- Token --- */
typedef enum {
	TOK_EOF, TOK_NEWLINE,
	TOK_INT, TOK_FLOAT, TOK_STR, TOK_ID,
	TOK_PRAGMA,   /* !target !stack 等 */
	TOK_COLON, TOK_LBRACE, TOK_RBRACE,
	TOK_LBRACKET, TOK_RBRACKET
} TokenKind;

typedef struct Token {
	TokenKind kind;
	int64_t   val;
	double    dbl;    /* TOK_FLOAT 时使用 */
	char     *start;
	size_t    len;
	char     *str;    /* TOK_STR 时指向已分配字符串 */
	size_t    str_len;
} Token;

/* --- AST：程序 = 定义/编译指令序列；块 = 操作序列 --- */
typedef enum {
	OP_INT, OP_FLOAT, OP_STR, OP_WORD, OP_VAR, OP_CONST, OP_BLOCK, OP_IF, OP_SWITCH,
	OP_LIST_LITERAL, OP_FOR_EXT, OP_WHILE_INF, OP_FOR_CSTYLE, OP_FOR_RANGE, OP_EACH, OP_WHILE_COND
} OpKind;

typedef struct Op Op;
struct Op {
	OpKind kind;
	union {
		int64_t  i;
		double   d;     /* OP_FLOAT */
		struct { char *s; size_t len; } str;
		struct { char *name; size_t len; } word;
		int      var_slot;   /* OP_VAR：变量槽位下标 */
		int      const_slot; /* OP_CONST：常量槽位下标 */
		Op *block;   /* 块内第一条，用 next 串起 */
		struct { Op *cond; Op *then_b; Op *else_b; } iff;
		struct { int size; Op *elements; int count; int temp_slot; } list_literal;
		struct { int64_t start, end, step; char *var; size_t var_len; Op *body; } for_ext;
		struct { Op *body; } while_inf;
		struct { Op *init; Op *cond; Op *step; Op *body; } for_cstyle;
		struct { int64_t start, end; Op *body; char *var; size_t var_len; } for_range;
		struct { Op *list; Op *body; } each;
		struct { Op *cond; Op *body; } while_cond;
		struct { Op *value; Op *cases; Op *default_block; } switch_;
	} u;
	Op *next;
};

typedef struct Def {
	char *name;
	size_t name_len;
	char **params;   /* 参数名，param_count 个 */
	size_t *param_lens;
	int param_count;
	Op *body;
	struct Def *next;
} Def;

typedef struct Pragma {
	char *name;
	size_t name_len;
	char *arg;      /* 整行参数字符串，可选 */
	struct Pragma *next;
} Pragma;

/* 常量类型 */
typedef enum { CONST_INT, CONST_DOUBLE, CONST_STR } ConstKind;

typedef struct Program {
	Pragma *pragmas;
	Def *defs;
	Op *main_block;   /* main: { ... } 的块 */
	Op *init_ops;     /* 顶层的 x: 123 / y: "hello" 产生的初始化序列，会插在 main 前执行 */
	/* 全局变量 */
	char **var_names;
	size_t *var_lens;
	int var_count;
	int var_cap;
	/* 常量：const name : value */
	char **const_names;
	size_t *const_lens;
	ConstKind *const_kinds;
	int64_t *const_ints;
	double *const_doubles;
	char **const_strs;
	size_t *const_str_lens;
	int const_count;
	int const_cap;
} Program;

/* --- Compiler --- */
typedef struct {
	char *src;
	char *p;
	Token cur;
	Token peek;
	bool has_peek;
	FILE *out;
	const char *out_path;
	int label_id;
	/* 编译选项（可由 ! 指令设置） */
	char *opt_target;
	int opt_stack;
	int opt_heap;
} Compiler;

void lexer_init(Compiler *c);
void lexer_advance(void);
bool lexer_at(TokenKind k);
bool lexer_at_peek(TokenKind k);
bool lexer_eat(TokenKind k);
void lexer_expect(TokenKind k);
Token *lexer_cur(void);

Program *parser_parse(Compiler *c);
void codegen(Compiler *c, Program *prog);

#endif
