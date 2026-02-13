/* Mira lexer – # 注释  ! 编译指令  : 定义  { } 块  后置词 */
#include "mira.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Compiler *comp;

static void next_char(void) { if (*comp->p) comp->p++; }

static int is_ident_char(char c) {
	/* 允许以 '.' 开头的调试词（.s .r .t .vars 等） */
	return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '?' || c == '=' || c == '.';
}

static void read_token(Token *t) {
	const char *s = comp->p;
	while (*s == ' ' || *s == '\t' || *s == '\r') s++;
	if (*s == '\n') {
		t->kind = TOK_NEWLINE;
		t->len = 1;
		comp->p = (char *)s + 1;
		return;
	}
	if (*s == '#') {
		while (*s && *s != '\n') s++;
		comp->p = (char *)s;
		read_token(t);
		return;
	}
	t->start = (char *)s;
	t->len = 0;
	t->str = NULL;
	t->str_len = 0;
	if (!*s) { t->kind = TOK_EOF; comp->p = (char *)s; return; }
	comp->p = (char *)s;

	/* 编译指令 !target !stack 等；单独的 ! 是“写内存”词 */
	if (*s == '!') {
		if (isalpha((unsigned char)s[1]) || isdigit((unsigned char)s[1]) || s[1] == '-' || s[1] == '_') {
			next_char();
			s = comp->p;
			while (is_ident_char(*comp->p)) next_char();
			t->kind = TOK_PRAGMA;
			t->len = (size_t)(comp->p - s);
			t->start = (char *)s;
			return;
		}
		t->kind = TOK_ID;
		t->len = 1;
		next_char();
		return;
	}

	/* 数字（整数或小数，支持科学计数法 1e-3） */
	if (isdigit((unsigned char)*s)) {
		t->val = 0;
		while (isdigit((unsigned char)*comp->p)) {
			t->val = t->val * 10 + (*comp->p - '0');
			next_char();
		}
		if (*comp->p == '.' && isdigit((unsigned char)comp->p[1])) {
			next_char();
			while (isdigit((unsigned char)*comp->p)) next_char();
			if ((*comp->p == 'e' || *comp->p == 'E') &&
			    (isdigit((unsigned char)comp->p[1]) || ((comp->p[1] == '+' || comp->p[1] == '-') && isdigit((unsigned char)comp->p[2])))) {
				next_char();
				if (*comp->p == '+' || *comp->p == '-') next_char();
				while (isdigit((unsigned char)*comp->p)) next_char();
			}
			char buf[64];
			size_t n = (size_t)(comp->p - s);
			if (n >= sizeof buf) n = sizeof buf - 1;
			memcpy(buf, s, n);
			buf[n] = '\0';
			t->dbl = strtod(buf, NULL);
			t->kind = TOK_FLOAT;
		} else {
			t->kind = TOK_INT;
		}
		t->len = (size_t)(comp->p - t->start);
		return;
	}

	/* 字符串 "..." */
	if (*s == '"') {
		next_char();
		s = comp->p;
		t->kind = TOK_STR;
		while (*comp->p && *comp->p != '"') {
			if (*comp->p == '\\') next_char();
			next_char();
		}
		size_t len = (size_t)(comp->p - s);
		t->str = malloc(len + 1);
		memcpy(t->str, s, len);
		t->str[len] = '\0';
		t->str_len = len;
		if (*comp->p == '"') next_char();
		return;
	}

	/* : { } [ ] */
	if (*s == ':') { t->kind = TOK_COLON; t->len = 1; next_char(); return; }
	if (*s == '{') { t->kind = TOK_LBRACE; t->len = 1; next_char(); return; }
	if (*s == '}') { t->kind = TOK_RBRACE; t->len = 1; next_char(); return; }
	if (*s == '[') { t->kind = TOK_LBRACKET; t->len = 1; next_char(); return; }
	if (*s == ']') { t->kind = TOK_RBRACKET; t->len = 1; next_char(); return; }

	/* 标识符/词：字母数字_-?= 或单字符 + - * / @ ! < > = 等
	   注意：这里刻意把 ':' 排除在标识符之外，这样像 main: 这种写法会被分成
	   ID(main) + TOK_COLON 两个 token，便于 parser 处理 name: { ... } 这种定义语法。 */
	if (is_ident_char(*s) || *s == '+' || *s == '-' || *s == '*' || *s == '/' || *s == '%' ||
	    *s == '@' || *s == '<' || *s == '>' || *s == '=') {
		t->kind = TOK_ID;
		next_char();
		while (is_ident_char(*comp->p) || *comp->p == '+' || *comp->p == '-' || *comp->p == '*' ||
		       *comp->p == '/' || *comp->p == '%' || *comp->p == '@' || *comp->p == '<' ||
		       *comp->p == '>' || *comp->p == '=' || *comp->p == '!')
			next_char();
		t->len = (size_t)(comp->p - t->start);
		return;
	}

	t->kind = TOK_EOF;
}

void lexer_init(Compiler *c) {
	comp = c;
	comp->p = comp->src;
	read_token(&comp->cur);
}

void lexer_advance(void) {
	comp->has_peek = false;
	read_token(&comp->cur);
}

bool lexer_at(TokenKind k) { return comp->cur.kind == k; }
bool lexer_at_peek(TokenKind k) {
	if (!comp->has_peek) {
		/* 暂存当前 p，读下一个 token 到 peek，读完后 p 已在 peek 之后；恢复 p 到“当前 token 之后”，这样 advance() 会正常读到下一个 */
		char *saved_p = comp->p;
		read_token(&comp->peek);
		comp->p = saved_p;
		comp->has_peek = true;
	}
	return comp->peek.kind == k;
}
bool lexer_eat(TokenKind k) {
	if (comp->cur.kind != k) return false;
	lexer_advance();
	return true;
}
void lexer_expect(TokenKind k) {
	if (comp->cur.kind != k) {
		/* 简单语法错误提示：期望的 token 与实际的 token kind */
		fprintf(stderr, "syntax error: expected %d, got %d\n", (int)k, (int)comp->cur.kind);
		exit(1);
	}
	lexer_advance();
}
Token *lexer_cur(void) { return &comp->cur; }
