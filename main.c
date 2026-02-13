/* Mira compiler driver – import 预处理、编译 */
#include "mira.h"
#include <stdlib.h>
#include <string.h>

/* 已加载的 import 路径（避免循环引用） */
#define IMPORT_MAX 64
static const char *imported[IMPORT_MAX];
static int imported_count;

static bool already_imported(const char *resolved) {
	for (int i = 0; i < imported_count; i++)
		if (imported[i] && strcmp(imported[i], resolved) == 0)
			return true;
	return false;
}

static void push_imported(const char *path) {
	if (imported_count < IMPORT_MAX) {
		imported[imported_count++] = strdup(path);
	}
}

/* 解析 import 行，返回路径或 NULL。格式：import "path" 或 import path */
static char *parse_import_line(const char *line) {
	while (*line == ' ' || *line == '\t') line++;
	if (strncmp(line, "import", 6) != 0) return NULL;
	line += 6;
	if (*line != ' ' && *line != '\t') return NULL;
	while (*line == ' ' || *line == '\t') line++;
	if (*line == '"') {
		line++;
		const char *end = strchr(line, '"');
		if (!end) return NULL;
		size_t len = (size_t)(end - line);
		char *out = malloc(len + 1);
		if (!out) return NULL;
		memcpy(out, line, len);
		out[len] = '\0';
		return out;
	}
	/* import path （无引号） */
	const char *p = line;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
	size_t len = (size_t)(p - line);
	char *out = malloc(len + 1);
	if (!out) return NULL;
	memcpy(out, line, len);
	out[len] = '\0';
	return out;
}

/* 将 path 转为绝对路径（基于 base_dir），调用方需 free */
static char *resolve_path(const char *base_dir, const char *path) {
	size_t blen = strlen(base_dir);
	size_t plen = strlen(path);
	char *res = malloc(blen + plen + 2);
	if (!res) return NULL;
	memcpy(res, base_dir, blen);
	if (blen > 0 && base_dir[blen - 1] != '/' && base_dir[blen - 1] != '\\')
		res[blen++] = '\\';
	memcpy(res + blen, path, plen + 1);
	return res;
}

/* 获取文件所在目录，返回新分配字符串，调用方 free */
static char *dir_of(const char *path) {
	const char *last = strrchr(path, '/');
	if (!last) last = strrchr(path, '\\');
	if (!last) {
		char *cwd = malloc(3);
		if (cwd) { cwd[0] = '.'; cwd[1] = '\0'; }
		return cwd;
	}
	size_t len = (size_t)(last - path + 1);
	char *d = malloc(len + 1);
	if (!d) return NULL;
	memcpy(d, path, len);
	d[len] = '\0';
	return d;
}

/* 递归展开 import，返回合并后的源码，调用方 free */
static char *expand_imports(const char *path, const char *base_dir) {
	FILE *in = fopen(path, "rb");
	if (!in) {
		fprintf(stderr, "mira: cannot open %s\n", path);
		exit(1);
	}
	size_t cap = 8192;
	char *raw = malloc(cap);
	size_t n = 0;
	for (int c; (c = fgetc(in)) != EOF;) {
		if (n + 1 >= cap) { cap *= 2; raw = realloc(raw, cap); }
		raw[n++] = (char)c;
	}
	raw[n] = '\0';
	fclose(in);

	char *out = malloc(1);
	if (!out) { free(raw); return NULL; }
	size_t out_cap = 1, out_len = 0;

	const char *line = raw;
	while (*line) {
		const char *eol = strchr(line, '\n');
		if (!eol) eol = line + strlen(line);
		size_t line_len = (size_t)(eol - line);
		if (line_len > 0 && line[line_len - 1] == '\r') line_len--;

		char *imp_path = parse_import_line(line);
		if (imp_path) {
			char *resolved = resolve_path(base_dir, imp_path);
			free(imp_path);
			if (!resolved) { free(raw); free(out); return NULL; }
			if (already_imported(resolved)) {
				free(resolved);
				line = eol + (eol[0] ? 1 : 0);
				continue;
			}
			push_imported(resolved);
			char *child_dir = dir_of(resolved);
			char *child_src = expand_imports(resolved, child_dir);
			free(child_dir);
			free(resolved);
			if (!child_src) { free(raw); free(out); return NULL; }
			size_t clen = strlen(child_src);
			while (out_len + clen + 2 >= out_cap) {
				out_cap *= 2;
				out = realloc(out, out_cap);
				if (!out) { free(raw); free(child_src); return NULL; }
			}
			memcpy(out + out_len, child_src, clen + 1);
			out_len += clen;
			if (clen > 0 && child_src[clen - 1] != '\n') {
				out[out_len++] = '\n';
				out[out_len] = '\0';
			}
			free(child_src);
		} else {
			while (out_len + line_len + 2 >= out_cap) {
				out_cap *= 2;
				out = realloc(out, out_cap);
				if (!out) { free(raw); return NULL; }
			}
			memcpy(out + out_len, line, line_len);
			out_len += line_len;
			out[out_len++] = '\n';
			out[out_len] = '\0';
		}
		line = eol + (eol[0] ? 1 : 0);
	}
	free(raw);
	return out;
}

void compile_file(const char *path, const char *out_asm_path) {
	imported_count = 0;
	for (int i = 0; i < IMPORT_MAX; i++) imported[i] = NULL;

	char *base_dir = dir_of(path);
	if (!base_dir) { fprintf(stderr, "mira: out of memory\n"); exit(1); }
	char *src = expand_imports(path, base_dir);
	free(base_dir);
	if (!src) exit(1);

	Compiler c = {0};
	c.src = src;
	c.out_path = out_asm_path;
	c.out = fopen(out_asm_path, "w");
	if (!c.out) {
		fprintf(stderr, "mira: cannot write %s\n", out_asm_path);
		exit(1);
	}

	Program *prog = parser_parse(&c);
	codegen(&c, prog);
	fflush(c.out);
	if (ferror(c.out)) {
		fprintf(stderr, "mira: write error\n");
		fclose(c.out);
		free(src);
		exit(1);
	}
	fclose(c.out);
	free(src);
	for (int i = 0; i < imported_count; i++) free((void *)imported[i]);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: mira <file.mira> [out.asm]\n");
		return 1;
	}
	const char *in = argv[1];
	const char *out = argc > 2 ? argv[2] : "out.asm";
	compile_file(in, out);
	return 0;
}
