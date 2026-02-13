/* Mira parser – 定义、块、main: { ... } */
#include "mira.h"
#include <stdlib.h>
#include <string.h>

static Compiler *comp;

static Op *parse_block_content(Program *prog);
static Op *parse_body_until_newline(Program *prog);
static Op *parse_block_until_loop(Program *prog);
static Op *parse_one(Program *prog);

/* 移除链表最后 n 个，返回被移除部分的头。若不足 n 个则返回 NULL 且不改动。 */
static Op *pop_last_n(Op **head, Op **tail, int n) {
	if (!*head || n <= 0) return NULL;
	int len = 0;
	for (Op *p = *head; p; p = p->next) len++;
	if (len < n) return NULL;
	Op *removed;
	if (len == n) {
		removed = *head;
		*head = NULL;
		*tail = NULL;
	} else {
		Op *cut = *head;
		for (int i = 0; i < len - n - 1; i++) cut = cut->next;
		removed = cut->next;
		cut->next = NULL;
		*tail = cut;
	}
	return removed;
}

/* 从链表中取出整数值，仅 OP_INT 或 OP_CONST(int) */
static int64_t op_to_int(Op *o, Program *prog) {
	if (o->kind == OP_INT) return o->u.i;
	if (o->kind == OP_CONST) {
		int s = o->u.const_slot;
		if (prog->const_kinds[s] == CONST_INT) return prog->const_ints[s];
	}
	return 0;
}

/* 在程序中登记一个变量，返回槽位下标 */
static int prog_add_var(Program *prog, char *name, size_t len) {
	for (int i = 0; i < prog->var_count; i++)
		if (prog->var_lens[i] == len && memcmp(prog->var_names[i], name, len) == 0)
			return i;
	if (prog->var_count >= prog->var_cap) {
		prog->var_cap = prog->var_cap ? prog->var_cap * 2 : 8;
		prog->var_names = realloc(prog->var_names, (size_t)prog->var_cap * sizeof(char *));
		prog->var_lens = realloc(prog->var_lens, (size_t)prog->var_cap * sizeof(size_t));
	}
	prog->var_names[prog->var_count] = name;
	prog->var_lens[prog->var_count] = len;
	return prog->var_count++;
}

static int prog_var_slot(Program *prog, const char *name, size_t len) {
	for (int i = 0; i < prog->var_count; i++)
		if (prog->var_lens[i] == len && memcmp(prog->var_names[i], name, len) == 0)
			return i;
	return -1;
}

/* 常量表 */
static int prog_add_const(Program *prog, char *name, size_t len, ConstKind k, int64_t vi, double vd, char *vs, size_t vslen) {
	for (int i = 0; i < prog->const_count; i++)
		if (prog->const_lens[i] == len && memcmp(prog->const_names[i], name, len) == 0)
			return i;
	if (prog->const_count >= prog->const_cap) {
		prog->const_cap = prog->const_cap ? prog->const_cap * 2 : 8;
		prog->const_names = realloc(prog->const_names, (size_t)prog->const_cap * sizeof(char *));
		prog->const_lens = realloc(prog->const_lens, (size_t)prog->const_cap * sizeof(size_t));
		prog->const_kinds = realloc(prog->const_kinds, (size_t)prog->const_cap * sizeof(ConstKind));
		prog->const_ints = realloc(prog->const_ints, (size_t)prog->const_cap * sizeof(int64_t));
		prog->const_doubles = realloc(prog->const_doubles, (size_t)prog->const_cap * sizeof(double));
		prog->const_strs = realloc(prog->const_strs, (size_t)prog->const_cap * sizeof(char *));
		prog->const_str_lens = realloc(prog->const_str_lens, (size_t)prog->const_cap * sizeof(size_t));
	}
	int i = prog->const_count++;
	prog->const_names[i] = name;
	prog->const_lens[i] = len;
	prog->const_kinds[i] = k;
	prog->const_ints[i] = vi;
	prog->const_doubles[i] = vd;
	prog->const_strs[i] = vs;
	prog->const_str_lens[i] = vslen;
	return i;
}

static int prog_const_slot(Program *prog, const char *name, size_t len) {
	for (int i = 0; i < prog->const_count; i++)
		if (prog->const_lens[i] == len && memcmp(prog->const_names[i], name, len) == 0)
			return i;
	return -1;
}

static Op *new_op(OpKind k) {
	Op *o = calloc(1, sizeof(Op));
	o->kind = k;
	return o;
}

/* 构造 value_op -> var_addr -> ! 的链，用于 x: 123 / y: "hello" */
static Op *make_assign_chain(Program *prog, char *name, size_t len, Op *value_op) {
	int slot = prog_add_var(prog, name, len);
	Op *var_op = new_op(OP_VAR);
	var_op->u.var_slot = slot;
	Op *store_op = new_op(OP_WORD);
	store_op->u.word.name = "!";  /* 栈式：值 地址 ! */
	store_op->u.word.len = 1;
	value_op->next = var_op;
	var_op->next = store_op;
	return value_op;
}

/* 列表字面量用到的临时变量名计数 */
static int list_literal_id;

/* 解析单条：整数、浮点、字符串、词、变量、常量、块、列表字面量、或 if */
static Op *parse_one(Program *prog) {
	Token *t = lexer_cur();
	if (lexer_at(TOK_INT)) {
		int64_t size_val = t->val;
		if (lexer_at_peek(TOK_LBRACKET)) {
			/* size [ elem ... ] 列表字面量 */
			lexer_advance();
			lexer_expect(TOK_LBRACKET);
			Op *elem_head = NULL, *elem_tail = NULL;
			int count = 0;
			while (!lexer_at(TOK_RBRACKET) && !lexer_at(TOK_EOF)) {
				Op *e = parse_one(prog);
				if (!e) break;
				if (!elem_head) elem_head = elem_tail = e;
				else { elem_tail->next = e; elem_tail = e; }
				while (elem_tail->next) elem_tail = elem_tail->next;
				count++;
			}
			lexer_expect(TOK_RBRACKET);
			if (count != (int)size_val) {
				fprintf(stderr, "list literal: size %lld does not match element count %d\n", (long long)size_val, count);
				exit(1);
			}
			{
				char *tmp_name = malloc(16);
				if (!tmp_name) { perror("malloc"); exit(1); }
				snprintf(tmp_name, 16, "__L%d", list_literal_id++);
				int slot = prog_add_var(prog, tmp_name, (size_t)strlen(tmp_name));
				Op *lit = new_op(OP_LIST_LITERAL);
				lit->u.list_literal.size = (int)size_val;
				lit->u.list_literal.elements = elem_head;
				lit->u.list_literal.count = count;
				lit->u.list_literal.temp_slot = slot;
				return lit;
			}
		}
		Op *o = new_op(OP_INT);
		o->u.i = size_val;
		lexer_advance();
		return o;
	}
	if (lexer_at(TOK_FLOAT)) {
		Op *o = new_op(OP_FLOAT);
		o->u.d = t->dbl;
		lexer_advance();
		return o;
	}
	if (lexer_at(TOK_STR)) {
		Op *o = new_op(OP_STR);
		o->u.str.s = t->str;
		o->u.str.len = t->str_len;
		lexer_advance();
		return o;
	}
	if (lexer_at(TOK_ID)) {
		char *name = t->start;
		size_t len = t->len;
		lexer_advance();
		int cslot = prog_const_slot(prog, name, len);
		if (cslot >= 0) {
			Op *o = new_op(OP_CONST);
			o->u.const_slot = cslot;
			return o;
		}
		int vslot = prog_var_slot(prog, name, len);
		if (vslot >= 0) {
			Op *o = new_op(OP_VAR);
			o->u.var_slot = vslot;
			return o;
		}
		Op *o = new_op(OP_WORD);
		o->u.word.name = name;
		o->u.word.len = len;
		return o;
	}
	if (lexer_at(TOK_LBRACE)) {
		lexer_advance();
		Op *block = new_op(OP_BLOCK);
		block->u.block = parse_block_content(prog);
		lexer_expect(TOK_RBRACE);
		return block;
	}
	return NULL;
}

/* 解析块内容直到遇到 loop（消费 loop） */
static Op *parse_block_until_loop(Program *prog) {
	Op *head = NULL, *tail = NULL;
	unsigned iter = 0;
	const unsigned max_iter = 100000;
	while (!lexer_at(TOK_EOF)) {
		if (++iter > max_iter) {
			fprintf(stderr, "parser: too many tokens before loop\n");
			exit(1);
		}
		while (lexer_eat(TOK_NEWLINE)) {
			if (lexer_at(TOK_EOF)) break;
		}
		if (lexer_at(TOK_EOF)) break;
		/* 遇到 loop 则结束 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 4 && memcmp(lexer_cur()->start, "loop", 4) == 0) {
			lexer_advance();
			break;
		}
		if (lexer_at(TOK_ID) && lexer_at_peek(TOK_COLON)) {
			char *name = lexer_cur()->start;
			size_t len = lexer_cur()->len;
			lexer_advance();
			lexer_advance();
			Op *expr_head = NULL, *expr_tail = NULL;
			while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF) &&
			       !(lexer_at(TOK_ID) && lexer_cur()->len == 4 && memcmp(lexer_cur()->start, "loop", 4) == 0)) {
				Op *o = parse_one(prog);
				if (!o) break;
				if (!expr_head) expr_head = expr_tail = o; else { expr_tail->next = o; expr_tail = o; }
			}
			if (expr_head) {
				int slot = prog_add_var(prog, name, len);
				Op *var_op = new_op(OP_VAR);
				var_op->u.var_slot = slot;
				Op *store_op = new_op(OP_WORD);
				store_op->u.word.name = "!";
				store_op->u.word.len = 1;
				for (expr_tail = expr_head; expr_tail->next; expr_tail = expr_tail->next);
				expr_tail->next = var_op;
				var_op->next = store_op;
				if (!head) head = tail = expr_head; else { tail->next = expr_head; }
				while (tail->next) tail = tail->next;
				continue;
			}
		}
		Op *o = parse_one(prog);
		if (!o) break;
		if (!head) head = tail = o; else { tail->next = o; tail = o; }
	}
	return head;
}

/* 解析块内容直到 } */
static Op *parse_block_content(Program *prog) {
	Op *head = NULL, *tail = NULL;
	unsigned iter = 0;
	const unsigned max_iter = 100000;
	while (!lexer_at(TOK_RBRACE) && !lexer_at(TOK_EOF)) {
		if (++iter > max_iter) {
			fprintf(stderr, "parser: too many tokens in block (possible infinite loop)\n");
			exit(1);
		}
		while (lexer_eat(TOK_NEWLINE)) {
			if (lexer_at(TOK_RBRACE) || lexer_at(TOK_EOF)) break;
		}
		if (lexer_at(TOK_RBRACE) || lexer_at(TOK_EOF)) break;

		/* const id : value —— 块内也可声明常量 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 5 && memcmp(lexer_cur()->start, "const", 5) == 0) {
			lexer_advance();
			if (!lexer_at(TOK_ID)) { fprintf(stderr, "syntax error: const expects identifier\n"); exit(1); }
			char *cname = lexer_cur()->start;
			size_t clen = lexer_cur()->len;
			lexer_advance();
			lexer_expect(TOK_COLON);
			if (lexer_at(TOK_INT)) {
				prog_add_const(prog, cname, clen, CONST_INT, lexer_cur()->val, 0, NULL, 0);
				lexer_advance();
			} else if (lexer_at(TOK_FLOAT)) {
				prog_add_const(prog, cname, clen, CONST_DOUBLE, 0, lexer_cur()->dbl, NULL, 0);
				lexer_advance();
			} else if (lexer_at(TOK_STR)) {
				prog_add_const(prog, cname, clen, CONST_STR, 0, 0, lexer_cur()->str, lexer_cur()->str_len);
				lexer_advance();
			} else {
				fprintf(stderr, "syntax error: const expects number or string after ':'\n");
				exit(1);
			}
			continue;
		}

		/* id : 123 或 id : "hello" 或 id : 3 list-new —— 变量定义/赋值 */
		if (lexer_at(TOK_ID) && lexer_at_peek(TOK_COLON)) {
			char *name = lexer_cur()->start;
			size_t len = lexer_cur()->len;
			lexer_advance();
			lexer_advance();
			/* 仅当右侧是单独一个整数且本行无其它 token 时用快速分支 */
			if (lexer_at(TOK_INT) && (lexer_at_peek(TOK_NEWLINE) || lexer_at_peek(TOK_RBRACE) || lexer_at_peek(TOK_EOF))) {
				Op *v = new_op(OP_INT);
				v->u.i = lexer_cur()->val;
				lexer_advance();
				Op *chain = make_assign_chain(prog, name, len, v);
				if (!head) head = tail = chain; else { tail->next = chain; }
				while (tail->next) tail = tail->next;
				continue;
			}
			/* 仅当右侧是单独一个字符串且本行无其它 token 时用快速分支 */
			if (lexer_at(TOK_STR) && (lexer_at_peek(TOK_NEWLINE) || lexer_at_peek(TOK_RBRACE) || lexer_at_peek(TOK_EOF))) {
				Op *v = new_op(OP_STR);
				v->u.str.s = lexer_cur()->str;
				v->u.str.len = lexer_cur()->str_len;
				lexer_advance();
				Op *chain = make_assign_chain(prog, name, len, v);
				if (!head) head = tail = chain; else { tail->next = chain; }
				while (tail->next) tail = tail->next;
				continue;
			}
			/* x: 表达式 —— 解析到换行，再接 var 和 ! */
			{
				Op *expr_head = NULL, *expr_tail = NULL;
				while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_RBRACE) && !lexer_at(TOK_EOF)) {
					Op *o = parse_one(prog);
					if (!o) break;
					if (!expr_head) expr_head = expr_tail = o; else { expr_tail->next = o; expr_tail = o; }
				}
				if (!expr_head) {
					fprintf(stderr, "syntax error: variable assignment expects value or expression after ':'\n");
					exit(1);
				}
				int slot = prog_add_var(prog, name, len);
				Op *var_op = new_op(OP_VAR);
				var_op->u.var_slot = slot;
				Op *store_op = new_op(OP_WORD);
				store_op->u.word.name = "!";
				store_op->u.word.len = 1;
				expr_tail->next = var_op;
				var_op->next = store_op;
				if (!head) head = tail = expr_head; else { tail->next = expr_head; }
				while (tail->next) tail = tail->next;
				continue;
			}
		}

		/* { init } { cond } { step } { body } for：类C风格 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 3 && memcmp(lexer_cur()->start, "for", 3) == 0) {
			int n = 0;
			for (Op *p = head; p; p = p->next) n++;
			if (n >= 4) {
				Op *p = head;
				for (int i = 0; i < n - 4; i++) p = p->next;
				Op *a = p, *b = p ? p->next : NULL, *c = b ? b->next : NULL, *d = c ? c->next : NULL;
				if (a && b && c && d && a->kind == OP_BLOCK && b->kind == OP_BLOCK &&
				    c->kind == OP_BLOCK && d->kind == OP_BLOCK) {
					lexer_advance();
					Op *removed = pop_last_n(&head, &tail, 4);
					Op *fo = new_op(OP_FOR_CSTYLE);
					fo->u.for_cstyle.init = removed;
					fo->u.for_cstyle.cond = removed->next;
					fo->u.for_cstyle.step = removed->next ? removed->next->next : NULL;
					fo->u.for_cstyle.body = removed->next && removed->next->next ? removed->next->next->next : NULL;
					if (!head) head = tail = fo; else { tail->next = fo; tail = fo; }
					continue;
				}
			}
		}
		/* 变量 起始 结束 for { body }：Python 风格，变量可修改 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 3 && memcmp(lexer_cur()->start, "for", 3) == 0 &&
		    lexer_at_peek(TOK_LBRACE)) {
			Op *last3 = NULL, *last2 = NULL, *last1 = NULL;
			for (Op *p = head; p; p = p->next) { last3 = last2; last2 = last1; last1 = p; }
			if (last1 && last2 && last3 && (last1->kind == OP_INT || last1->kind == OP_CONST) &&
			    (last2->kind == OP_INT || last2->kind == OP_CONST) && last3->kind == OP_WORD) {
				/* 必须在解析 block 前把循环变量加入 prog，否则 body 内的 i 会解析成 unknown word */
				char *var = last3->u.word.name;
				size_t var_len = last3->u.word.len;
				if (prog_var_slot(prog, var, var_len) < 0)
					prog_add_var(prog, var, var_len);
				lexer_advance(); /* for */
				Op *block = parse_one(prog);
				if (!block || block->kind != OP_BLOCK) {
					fprintf(stderr, "syntax error: for expects block after it\n");
					exit(1);
				}
				if (!head) head = tail = block; else { tail->next = block; tail = block; }
				Op *removed = pop_last_n(&head, &tail, 4);
				/* removed = var, start, end, block 顺序 */
				int64_t start = removed->next && (removed->next->kind == OP_INT || removed->next->kind == OP_CONST) ?
					(removed->next->kind == OP_INT ? removed->next->u.i : op_to_int(removed->next, prog)) : 0;
				int64_t end = removed->next && removed->next->next && (removed->next->next->kind == OP_INT || removed->next->next->kind == OP_CONST) ?
					(removed->next->next->kind == OP_INT ? removed->next->next->u.i : op_to_int(removed->next->next, prog)) : 0;
				Op *fo = new_op(OP_FOR_RANGE);
				fo->u.for_range.start = start;
				fo->u.for_range.end = end;
				fo->u.for_range.body = removed->next && removed->next->next ? removed->next->next->next : NULL;
				fo->u.for_range.var = var;
				fo->u.for_range.var_len = var_len;
				if (!head) head = tail = fo; else { tail->next = fo; tail = fo; }
				continue;
			}
		}
		/* 起始 结束 for { body }：简化版范围循环（for 后有 block） */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 3 && memcmp(lexer_cur()->start, "for", 3) == 0 &&
		    lexer_at_peek(TOK_LBRACE)) {
			Op *last2 = NULL, *last1 = NULL;
			for (Op *p = head; p; p = p->next) { last2 = last1; last1 = p; }
			if (last1 && last2 && (last1->kind == OP_INT || last1->kind == OP_CONST) &&
			    (last2->kind == OP_INT || last2->kind == OP_CONST)) {
				lexer_advance(); /* for */
				Op *block = parse_one(prog);
				if (!block || block->kind != OP_BLOCK) {
					fprintf(stderr, "syntax error: for expects block after it\n");
					exit(1);
				}
				if (!head) head = tail = block; else { tail->next = block; tail = block; }
				Op *removed = pop_last_n(&head, &tail, 3);
				int64_t start = removed->kind == OP_INT ? removed->u.i : op_to_int(removed, prog);
				int64_t end = removed->next ? (removed->next->kind == OP_INT ? removed->next->u.i : op_to_int(removed->next, prog)) : 0;
				Op *fo = new_op(OP_FOR_RANGE);
				fo->u.for_range.start = start;
				fo->u.for_range.end = end;
				fo->u.for_range.body = removed->next ? removed->next->next : NULL;
				fo->u.for_range.var = NULL;
				fo->u.for_range.var_len = 0;
				if (!head) head = tail = fo; else { tail->next = fo; tail = fo; }
				continue;
			}
		}
		/* 列表 each { body } */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 4 && memcmp(lexer_cur()->start, "each", 4) == 0) {
			if (tail && tail->kind == OP_LIST_LITERAL) {
				lexer_advance();
				Op *o = parse_one(prog);
				if (!o) {
					fprintf(stderr, "syntax error: each expects block after it\n");
					exit(1);
				}
				if (!head) head = tail = o; else { tail->next = o; tail = o; }
				Op *removed = pop_last_n(&head, &tail, 2);
				Op *ea = new_op(OP_EACH);
				ea->u.each.list = removed;
				ea->u.each.body = removed->next;
				if (!head) head = tail = ea; else { tail->next = ea; tail = ea; }
				continue;
			}
		}

		/* 条件 { then } { else } if 或 条件 { then } if：else 可省略 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 2 && memcmp(lexer_cur()->start, "if", 2) == 0) {
			Op *last2 = NULL, *last1 = NULL;
			for (Op *p = head; p; p = p->next) { last2 = last1; last1 = p; }
			if (last1 && last2 && last1->kind == OP_BLOCK && last2->kind == OP_BLOCK) {
				lexer_advance();
				Op *prev = NULL, *p = head;
				for (; p && p->kind != OP_BLOCK; prev = p, p = p->next) {}
				if (!p || !p->next || p->next->kind != OP_BLOCK) continue;
				Op *cond_head = head;
				Op *then_b = p;
				Op *else_b = p->next;
				if (prev) {
					prev->next = NULL;
					tail = prev;
				} else {
					head = NULL;
					tail = NULL;
				}
				Op *iff = new_op(OP_IF);
				iff->u.iff.cond = cond_head;
				iff->u.iff.then_b = then_b;
				iff->u.iff.else_b = else_b;
				head = tail = iff;
				continue;
			}
			/* 条件 { then } if：无 else */
			if (last1 && last1->kind == OP_BLOCK) {
				lexer_advance();
				Op *prev = NULL, *p = head;
				for (; p && p->kind != OP_BLOCK; prev = p, p = p->next) {}
				if (!p || !prev) continue;  /* 需要 cond 和 block */
				Op *cond_head = head;
				Op *then_b = p;
				if (prev) prev->next = NULL; else head = NULL;
				tail = prev;
				Op *iff = new_op(OP_IF);
				iff->u.iff.cond = cond_head;
				iff->u.iff.then_b = then_b;
				iff->u.iff.else_b = NULL;
				head = tail = iff;
				continue;
			}
		}
		/* 值 ... switch：支持 有/无 default。有 default：值 ... default { 默认 } switch；无 default：值 1 { one } 2 { two } switch */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 6 && memcmp(lexer_cur()->start, "switch", 6) == 0) {
			Op *last2 = NULL, *last1 = NULL;
			for (Op *p = head; p; p = p->next) { last2 = last1; last1 = p; }
			/* 有 default：last2 是 "default" 这个词 */
			if (last1 && last2 && last1->kind == OP_BLOCK &&
			    last2->kind == OP_WORD && last2->u.word.len == 7 &&
			    memcmp(last2->u.word.name, "default", 7) == 0) {
				lexer_advance();
				int len = 0;
				for (Op *p = head; p; p = p->next) len++;
				if (len < 4) continue;
				/* 找第一个 (pattern,block)：pattern 在 block 前 */
				Op *first_block = NULL;
				for (Op *p = head; p; p = p->next)
					if (p->kind == OP_BLOCK) { first_block = p; break; }
				if (!first_block) continue;
				Op *p1 = NULL;
				for (Op *p = head; p != first_block; p1 = p, p = p->next) {}
				/* n_pop = 从 p1 到 default_block 的个数 */
				int n_fixed = 0;
				for (Op *p = p1; p; p = p->next) n_fixed++;
				/* value 长度：p1 前有多少 op 属于 value。试探：若 p1 前是 @ 则 value=2，否则 1 */
				int val_len = 1;
				if (p1) {
					Op *before_p1 = NULL;
					for (Op *p = head; p != p1; before_p1 = p, p = p->next) {}
					if (before_p1 && before_p1->kind == OP_WORD &&
					    before_p1->u.word.len == 1 && before_p1->u.word.name[0] == '@')
						val_len = 2;
				}
				int n_pop = val_len + n_fixed;
				if (n_pop > len) n_pop = n_fixed;
				Op *removed = pop_last_n(&head, &tail, n_pop);
				if (!removed) continue;
				Op *default_block = removed;
				for (; default_block->next; default_block = default_block->next) {}
				Op *prev = removed;
				for (; prev->next && !(prev->next->kind == OP_WORD && prev->next->u.word.len == 7 &&
				    memcmp(prev->next->u.word.name, "default", 7) == 0); prev = prev->next) {}
				if (!prev->next) continue;
				prev->next = NULL;
				/* removed 含 value+cases+default；分出 value 与 cases */
				Op *value = removed;
				Op *cases = value->next;
				Op *first_blk = NULL;
				for (Op *p = removed; p; p = p->next)
					if (p->kind == OP_BLOCK) { first_blk = p; break; }
				if (first_blk) {
					Op *first_pat = NULL;
					for (Op *p = removed; p != first_blk; first_pat = p, p = p->next) {}
					if (first_pat) {
						Op *val_last = NULL;
						for (Op *p = removed; p != first_pat; val_last = p, p = p->next) {}
						if (val_last) { val_last->next = NULL; value = removed; cases = first_pat; }
					}
				}
				Op *sw = new_op(OP_SWITCH);
				sw->u.switch_.value = value;
				sw->u.switch_.cases = cases;
				sw->u.switch_.default_block = default_block;
				if (!head) head = tail = sw; else { tail->next = sw; tail = sw; }
				continue;
			}
			/* 无 default：值 1 { one } 2 { two } switch */
			if (last1 && last2 && last1->kind == OP_BLOCK &&
			    (last2->kind == OP_INT || last2->kind == OP_CONST)) {
				lexer_advance();
				int len = 0;
				for (Op *p = head; p; p = p->next) len++;
				if (len < 3) continue;  /* 至少 value + pat + block */
				Op *first_block = NULL;
				for (Op *p = head; p; p = p->next)
					if (p->kind == OP_BLOCK) { first_block = p; break; }
				if (!first_block) continue;
				Op *p1 = NULL;
				for (Op *p = head; p != first_block; p1 = p, p = p->next) {}
				int n_fixed = 0;
				for (Op *p = p1; p; p = p->next) n_fixed++;
				int val_len = 1;
				if (p1) {
					Op *before_p1 = NULL;
					for (Op *p = head; p != p1; before_p1 = p, p = p->next) {}
					if (before_p1 && before_p1->kind == OP_WORD &&
					    before_p1->u.word.len == 1 && before_p1->u.word.name[0] == '@')
						val_len = 2;
				}
				int n_pop = val_len + n_fixed;
				if (n_pop > len) n_pop = n_fixed;
				Op *removed = pop_last_n(&head, &tail, n_pop);
				if (!removed) continue;
				Op *value = removed;
				Op *cases = value->next;
				Op *first_blk = NULL;
				for (Op *p = removed; p; p = p->next)
					if (p->kind == OP_BLOCK) { first_blk = p; break; }
				if (first_blk) {
					Op *first_pat = NULL;
					for (Op *p = removed; p != first_blk; first_pat = p, p = p->next) {}
					if (first_pat) {
						Op *val_last = NULL;
						for (Op *p = removed; p != first_pat; val_last = p, p = p->next) {}
						if (val_last) { val_last->next = NULL; value = removed; cases = first_pat; }
					}
				}
				Op *sw = new_op(OP_SWITCH);
				sw->u.switch_.value = value;
				sw->u.switch_.cases = cases;
				sw->u.switch_.default_block = NULL;
				if (!head) head = tail = sw; else { tail->next = sw; tail = sw; }
				continue;
			}
			/* 值 { 模式1 { 代码1 } 模式2 { 代码2 } default { 默认代码 } } switch：块格式 */
			if (last1 && last2 && last1->kind == OP_BLOCK) {
				lexer_advance();
				Op *prev = NULL, *p = head;
				for (; p && p->kind != OP_BLOCK; prev = p, p = p->next) {}
				if (!p) continue;
				Op *value = prev ? head : NULL;
				Op *block = p;
				if (prev) prev->next = NULL; else head = NULL;
				tail = prev;
				Op *content = block->u.block;
				if (!content) continue;
				Op *last = content;
				for (; last->next; last = last->next) {}
				if (last->kind != OP_BLOCK) continue;
				Op *default_block = last;
				Op *prev2 = content;
				for (; prev2->next && !(prev2->next->kind == OP_WORD && prev2->next->u.word.len == 7 &&
				    memcmp(prev2->next->u.word.name, "default", 7) == 0); prev2 = prev2->next) {}
				if (!prev2->next) continue;
				prev2->next = NULL;
				Op *cases = content;
				Op *sw = new_op(OP_SWITCH);
				sw->u.switch_.value = value;
				sw->u.switch_.cases = cases;
				sw->u.switch_.default_block = default_block;
				head = tail = sw;
				continue;
			}
		}
		/* { cond } { body } while：条件 while */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 5 && memcmp(lexer_cur()->start, "while", 5) == 0) {
			Op *last2 = NULL, *last1 = NULL;
			for (Op *p = head; p; p = p->next) { last2 = last1; last1 = p; }
			if (last1 && last2 && last1->kind == OP_BLOCK && last2->kind == OP_BLOCK) {
				lexer_advance();
				Op *removed = pop_last_n(&head, &tail, 2);
				Op *wo = new_op(OP_WHILE_COND);
				wo->u.while_cond.cond = removed;
				wo->u.while_cond.body = removed->next;
				if (!head) head = tail = wo; else { tail->next = wo; tail = wo; }
				continue;
			}
			/* while ... loop 无限循环 */
			lexer_advance();
			lexer_eat(TOK_NEWLINE);
			Op *body = parse_block_until_loop(prog);
			Op *wo = new_op(OP_WHILE_INF);
			wo->u.while_inf.body = body;
			if (!head) head = tail = wo; else { tail->next = wo; tail = wo; }
			continue;
		}

		Op *o = parse_one(prog);
		if (!o) break;
		if (!head) head = tail = o; else { tail->next = o; tail = o; }
	}
	return head;
}

/* 解析定义体：到换行或 EOF */
static Op *parse_body_until_newline(Program *prog) {
	Op *head = NULL, *tail = NULL;
		unsigned iter_nl = 0;
		while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) {
		if (++iter_nl > 100000) {
			fprintf(stderr, "parser: too many tokens in definition body\n");
			exit(1);
		}
		if (lexer_at(TOK_ID) && lexer_cur()->len == 5 && memcmp(lexer_cur()->start, "const", 5) == 0) {
			lexer_advance();
			if (!lexer_at(TOK_ID)) { fprintf(stderr, "syntax error: const expects identifier\n"); exit(1); }
			char *cname = lexer_cur()->start;
			size_t clen = lexer_cur()->len;
			lexer_advance();
			lexer_expect(TOK_COLON);
			if (lexer_at(TOK_INT)) { prog_add_const(prog, cname, clen, CONST_INT, lexer_cur()->val, 0, NULL, 0); lexer_advance(); }
			else if (lexer_at(TOK_FLOAT)) { prog_add_const(prog, cname, clen, CONST_DOUBLE, 0, lexer_cur()->dbl, NULL, 0); lexer_advance(); }
			else if (lexer_at(TOK_STR)) { prog_add_const(prog, cname, clen, CONST_STR, 0, 0, lexer_cur()->str, lexer_cur()->str_len); lexer_advance(); }
			else { fprintf(stderr, "syntax error: const expects number or string after ':'\n"); exit(1); }
			continue;
		}
		if (lexer_at(TOK_ID) && lexer_at_peek(TOK_COLON)) {
			char *name = lexer_cur()->start;
			size_t len = lexer_cur()->len;
			lexer_advance();
			lexer_advance();
			if (lexer_at(TOK_INT) && (lexer_at_peek(TOK_NEWLINE) || lexer_at_peek(TOK_EOF))) {
				Op *v = new_op(OP_INT);
				v->u.i = lexer_cur()->val;
				lexer_advance();
				Op *chain = make_assign_chain(prog, name, len, v);
				if (!head) head = tail = chain; else { tail->next = chain; }
				while (tail->next) tail = tail->next;
				continue;
			}
			if (lexer_at(TOK_STR) && (lexer_at_peek(TOK_NEWLINE) || lexer_at_peek(TOK_EOF))) {
				Op *v = new_op(OP_STR);
				v->u.str.s = lexer_cur()->str;
				v->u.str.len = lexer_cur()->str_len;
				lexer_advance();
				Op *chain = make_assign_chain(prog, name, len, v);
				if (!head) head = tail = chain; else { tail->next = chain; }
				while (tail->next) tail = tail->next;
				continue;
			}
			/* id : 表达式 */
			Op *expr_head = NULL, *expr_tail = NULL;
			while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) {
				Op *o = parse_one(prog);
				if (!o) break;
				if (!expr_head) expr_head = expr_tail = o; else { expr_tail->next = o; expr_tail = o; }
			}
			if (expr_head) {
				int slot = prog_add_var(prog, name, len);
				Op *var_op = new_op(OP_VAR);
				var_op->u.var_slot = slot;
				Op *store_op = new_op(OP_WORD);
				store_op->u.word.name = "!"; store_op->u.word.len = 1;
				expr_tail->next = var_op; var_op->next = store_op;
				if (!head) head = tail = expr_head; else { tail->next = expr_head; }
				while (tail->next) tail = tail->next;
				continue;
			}
		}
		Op *o = parse_one(prog);
		if (!o) break;
		if (!head) head = tail = o; else { tail->next = o; tail = o; }
	}
	return head;
}

/* 判断当前 { ... } 是否为参数列表（仅含 ID） */
static bool parse_param_list(char ***out_names, size_t **out_lens, int *out_count) {
	if (!lexer_at(TOK_LBRACE)) return false;
	lexer_advance();
	char **names = NULL;
	size_t *lens = NULL;
	int n = 0, cap = 4;
	names = malloc((size_t)cap * sizeof(char *));
	lens = malloc((size_t)cap * sizeof(size_t));
	while (lexer_at(TOK_ID)) {
		if (n >= cap) { cap *= 2; names = realloc(names, (size_t)cap * sizeof(char *)); lens = realloc(lens, (size_t)cap * sizeof(size_t)); }
		names[n] = lexer_cur()->start;
		lens[n] = lexer_cur()->len;
		n++;
		lexer_advance();
	}
	lexer_expect(TOK_RBRACE);
	*out_names = names;
	*out_lens = lens;
	*out_count = n;
	return true;
}

Program *parser_parse(Compiler *c) {
	comp = c;
	c->p = c->src;
	lexer_init(c);
	Program *prog = calloc(1, sizeof(Program));

	while (!lexer_at(TOK_EOF)) {
		lexer_eat(TOK_NEWLINE);
		if (lexer_at(TOK_EOF)) break;

		if (lexer_at(TOK_PRAGMA)) {
			Pragma *p = calloc(1, sizeof(Pragma));
			p->name = lexer_cur()->start;
			p->name_len = lexer_cur()->len;
			lexer_advance();
			if (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) {
				p->arg = lexer_cur()->start;
				while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) lexer_advance();
			}
			p->next = prog->pragmas;
			prog->pragmas = p;
			continue;
		}

		/* const id : value —— 常量 */
		if (lexer_at(TOK_ID) && lexer_cur()->len == 5 && memcmp(lexer_cur()->start, "const", 5) == 0) {
			lexer_advance();
			if (!lexer_at(TOK_ID)) { fprintf(stderr, "syntax error: const expects identifier\n"); exit(1); }
			char *cname = lexer_cur()->start;
			size_t clen = lexer_cur()->len;
			lexer_advance();
			lexer_expect(TOK_COLON);
			if (lexer_at(TOK_INT)) {
				prog_add_const(prog, cname, clen, CONST_INT, lexer_cur()->val, 0, NULL, 0);
				lexer_advance();
			} else if (lexer_at(TOK_FLOAT)) {
				prog_add_const(prog, cname, clen, CONST_DOUBLE, 0, lexer_cur()->dbl, NULL, 0);
				lexer_advance();
			} else if (lexer_at(TOK_STR)) {
				prog_add_const(prog, cname, clen, CONST_STR, 0, 0, lexer_cur()->str, lexer_cur()->str_len);
				lexer_advance();
			} else {
				fprintf(stderr, "syntax error: const expects number or string after ':'\n");
				exit(1);
			}
			continue;
		}

		/* name: ... 或 main: { ... } 或 x: 123 / y: "hello" */
		if (lexer_at(TOK_ID)) {
			char *name = lexer_cur()->start;
			size_t name_len = lexer_cur()->len;

			lexer_advance();
			if (!lexer_at(TOK_COLON)) {
				while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) lexer_advance();
				continue;
			}

			lexer_advance();
			lexer_eat(TOK_NEWLINE);

			/* 顶层变量赋值：x: 123 或 y: "hello" */
			if (lexer_at(TOK_INT) || lexer_at(TOK_STR)) {
				Op *v;
				if (lexer_at(TOK_INT)) {
					v = new_op(OP_INT);
					v->u.i = lexer_cur()->val;
					lexer_advance();
				} else {
					v = new_op(OP_STR);
					v->u.str.s = lexer_cur()->str;
					v->u.str.len = lexer_cur()->str_len;
					lexer_advance();
				}
				Op *chain = make_assign_chain(prog, name, name_len, v);
				if (!prog->init_ops) prog->init_ops = chain;
				else {
					Op *t = prog->init_ops;
					while (t->next) t = t->next;
					t->next = chain;
				}
				continue;
			}

			/* main: { ... } */
			if (name_len == 4 && memcmp(name, "main", 4) == 0 && lexer_at(TOK_LBRACE)) {
				lexer_advance();            /* 吃掉 { */
				Op *main_block = parse_block_content(prog);
				lexer_expect(TOK_RBRACE);
				prog->main_block = main_block;
				continue;
			}

			/* 其它定义: name: { params } body 或 name: { body } */
			Def *d = calloc(1, sizeof(Def));
			d->name = name;
			d->name_len = name_len;
			d->next = NULL;
			if (!prog->defs) prog->defs = d;
			else {
				Def *tail = prog->defs;
				while (tail->next) tail = tail->next;
				tail->next = d;
			}

			if (lexer_at(TOK_LBRACE)) {
				/* 区分：{ a b } 参数列表 + body  vs  { body } 仅函数体 */
				if (lexer_at_peek(TOK_ID) || lexer_at_peek(TOK_RBRACE)) {
					/* add: { a b } a b + */
					char **params = NULL;
					size_t *param_lens = NULL;
					int nparam = 0;
					parse_param_list(&params, &param_lens, &nparam);
					d->params = params;
					d->param_lens = param_lens;
					d->param_count = nparam;
					d->body = parse_body_until_newline(prog);
				} else {
					/* foo: { body }  无参数 */
					lexer_expect(TOK_LBRACE);
					d->body = parse_block_content(prog);
					lexer_expect(TOK_RBRACE);
				}
			} else {
				fprintf(stderr, "syntax error: function/definition expects { ... }\n");
				exit(1);
			}
			continue;
		}

		/* 无法识别则跳过本行 */
		while (!lexer_at(TOK_NEWLINE) && !lexer_at(TOK_EOF)) lexer_advance();
	}

	/* 顶层 x: 123 等初始化插在 main 前执行 */
	if (prog->init_ops && prog->main_block) {
		Op *t = prog->init_ops;
		while (t->next) t = t->next;
		t->next = prog->main_block;
		prog->main_block = prog->init_ops;
	} else if (prog->init_ops) {
		prog->main_block = prog->init_ops;
	}

	return prog;
}
