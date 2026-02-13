/* Mira runtime – print / exit / 内存等辅助函数（栈式语法生成代码用） */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* 若汇编未定义 mira_main（例如 out.asm 为空），用此弱符号避免链接错误 */
#ifdef __GNUC__
__attribute__((weak)) void mira_main(void) {}
#else
void mira_main(void); /* 非 GCC 时仍由汇编提供 */
#endif

/* 由汇编导出的全局符号（在 codegen.c 里生成） */
extern long long mira_var_count;
extern long long mira_vars[];         /* .bss 里的变量槽数组（每槽 8 字节） */
extern const char *mira_var_names[];  /* 变量名字符串表 */

/* 供 print 使用的 float 临时缓冲区（C 定义，汇编可写入） */
double mira_float_tmp;

/* 简单运行时统计信息 */
static long long g_alloc_count = 0;
static long long g_alloc_bytes = 0;
static clock_t   g_start_clock = 0;

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	g_start_clock = clock();
	mira_main();
	return 0;
}

/* 统一 print：type: 0=int, 1=string(ptr), 2=float(ptr-to-double), 3=bool */
void mira_print(int type, long long val_lo) {
	switch (type) {
		case 0:
			printf("%lld\n", val_lo);
			break;
		case 1:
			if ((const char *)val_lo)
				printf("%s\n", (const char *)val_lo);
			break;
		case 2:
			/* val_lo 为 double* 指针 */
			if (val_lo) printf("%g\n", *(double *)(void *)val_lo);
			break;
		case 3:
			printf("%s\n", val_lo ? "true" : "false");
			break;
		default:
			printf("%lld\n", val_lo);
			break;
	}
}

/* read: 从 stdin 读一个整数，返回该值（供 codegen 压栈） */
long long mira_read_int(void) {
	long long x;
	if (scanf("%lld", &x) != 1)
		return 0;
	return x;
}

/* input: 从 stdin 读一行，返回字符串指针（malloc 分配，用完后需 free） */
char *mira_input(void) {
	static char buf[4096];
	if (!fgets(buf, sizeof(buf), stdin))
		return NULL;
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len--;
	}
	char *s = malloc(len + 1);
	if (!s) return NULL;
	memcpy(s, buf, len + 1);
	return s;
}

/* --- 内存管理：allocate/free/move/erase/dump --- */

void *mem_alloc(long long size) {
	if (size <= 0 || size > 0x7fffffff) return NULL;
	void *p = malloc((size_t)size);
	if (p) {
		g_alloc_count++;
		g_alloc_bytes += size;
	}
	return p;
}

void mem_free(void *ptr) {
	if (!ptr) return;
	free(ptr);
}

void mem_move(void *dst, void *src, long long size) {
	if (!dst || !src || size <= 0) return;
	memmove(dst, src, (size_t)size);
}

void mem_erase(void *dst, long long size) {
	if (!dst || size <= 0) return;
	memset(dst, 0, (size_t)size);
}

/* 简单内存十六进制 dump：addr size -> 打印 size 字节 */
void mira_mem_dump(void *addr, long long size) {
	unsigned char *p = (unsigned char *)addr;
	if (!p || size <= 0) {
		printf("[dump] invalid address or size (%p, %lld)\n", addr, size);
		return;
	}
	long long remaining = size;
	while (remaining > 0) {
		int line = (int)(remaining > 16 ? 16 : remaining);
		printf("%p: ", (void *)p);
		for (int i = 0; i < line; i++) {
			printf("%02X ", p[i]);
		}
		printf(" | ");
		for (int i = 0; i < line; i++) {
			unsigned char c = p[i];
			putchar(isprint(c) ? c : '.');
		}
		putchar('\n');
		p += line;
		remaining -= line;
	}
}

/* .s：打印 Mira 数据栈（mira_stack..r12 之间），不修改栈 */
void mira_dump_data_stack(void *base, void *sp) {
	long long depth = 0;
	if (sp && base && sp >= base) {
		depth = ((char *)sp - (char *)base) / 8;
	}
	printf("=== data stack (depth=%lld) ===\n", depth);
	long long *v = (long long *)base;
	for (long long i = 0; i < depth; i++) {
		printf("[%3lld] %lld (0x%llx)\n", i, v[i], (unsigned long long)v[i]);
	}
}

/* .var/.vars：基于编译期生成的变量名表和 mira_vars 槽，打印变量信息 */
void mira_dump_var_slot(int slot) {
	if (slot < 0 || (long long)slot >= mira_var_count) {
		printf("var[%d]: <invalid slot>\n", slot);
		return;
	}
	long long *vars = mira_vars;
	long long value = vars[slot];
	const char *name = mira_var_names ? mira_var_names[slot] : "?";
	printf("var %s [slot %d] @%p = %lld (0x%llx)\n",
	       name, slot, (void *)&vars[slot], value, (unsigned long long)value);
}

void mira_dump_vars(void) {
	printf("=== vars (count=%lld) ===\n", mira_var_count);
	for (int i = 0; (long long)i < mira_var_count; i++) {
		mira_dump_var_slot(i);
	}
}

/* .stats：简单执行统计 */
void mira_stats(void) {
	double seconds = 0.0;
	if (g_start_clock != 0) {
		seconds = (double)(clock() - g_start_clock) / (double)CLOCKS_PER_SEC;
	}
	printf("=== .stats ===\n");
	printf("alloc_count  = %lld\n", g_alloc_count);
	printf("alloc_bytes  = %lld\n", g_alloc_bytes);
	printf("var_count    = %lld\n", mira_var_count);
	printf("elapsed_time = %.3f s\n", seconds);
}

/* 调试/断点相关：用操作系统/调试器实现真正的单步，Mira 里只提供挂钩 */
void mira_debug_break(void) {
	/* 异步/非阻塞版：仅打印提示，不再真正触发断点中断 */
	printf("[break] breakpoint hit (non-blocking)\n");
}

void mira_debug_step(void) {
	/* 异步/非阻塞版：打印当前位置提示后直接返回，不等待输入 */
	printf("[step] reached step point (non-blocking)\n");
}

void mira_debug_next(void) {
	/* 异步/非阻塞版：同 step，占位用 */
	printf("[next] reached next point (non-blocking)\n");
}

void mira_debug_continue(void) {
	printf("[continue] 继续执行\n");
}

void mira_dump_return_stack(void) {
	printf("[.r] 当前实现直接使用 CPU 调用栈，没有单独的“返回栈”，此命令仅占位。\n");
}

void mira_dump_type_stack(void) {
	printf("[.t] 类型信息只在编译期跟踪，运行期已擦除，暂不支持真实类型栈 dump。\n");
}

void mira_backtrace(void) {
	printf("[.backtrace] 未实现运行时回溯，请使用系统调试器（如 VS / windbg）。\n");
}

void mira_where(void) {
	printf("[.where] 当前版本没有行号/源位置信息映射。\n");
}

void mira_watch_not_supported(void) {
	printf("[watch] 监视点需要更重的插桩，当前运行时未实现。\n");
}

/* --- 列表：布局 [length (8字节)][elem0][elem1]...，ptr 指向表头 --- */
void *mira_list_new(long long size) {
	if (size <= 0 || size > 0x7fffffff) return NULL;
	long long nbytes = 8 + size * 8;
	void *p = mem_alloc(nbytes);
	if (!p) return NULL;
	*(long long *)p = size;
	return p;
}

long long mira_list_len(void *ptr) {
	if (!ptr) return 0;
	return *(long long *)ptr;
}

long long mira_list_get(void *ptr, long long index) {
	if (!ptr) return 0;
	long long len = *(long long *)ptr;
	if (index < 0 || index >= len) return 0;
	return *(long long *)((char *)ptr + 8 + index * 8);
}

void mira_list_set(void *ptr, long long index, long long value) {
	if (!ptr) return;
	long long len = *(long long *)ptr;
	if (index < 0 || index >= len) return;
	*(long long *)((char *)ptr + 8 + index * 8) = value;
}

void mira_list_free(void *ptr) {
	mem_free(ptr);
}

/* --- 字典：布局 [count (8)][cap (8)][k0][v0][k1][v1]...，线性查找 --- */
void *mira_dict_new(long long cap) {
	if (cap <= 0 || cap > 0x7fffffff) return NULL;
	long long nbytes = 16 + cap * 16;
	void *p = mem_alloc(nbytes);
	if (!p) return NULL;
	*(long long *)p = 0;
	*(long long *)((char *)p + 8) = cap;
	return p;
}

void mira_dict_set(void *ptr, long long key, long long value) {
	if (!ptr) return;
	long long *count = (long long *)ptr;
	long long *cap = (long long *)ptr + 1;
	long long *pairs = (long long *)ptr + 2;
	for (long long i = 0; i < *count; i++) {
		if (pairs[i * 2] == key) {
			pairs[i * 2 + 1] = value;
			return;
		}
	}
	if (*count < *cap) {
		pairs[*count * 2] = key;
		pairs[*count * 2 + 1] = value;
		(*count)++;
	}
}

long long mira_dict_get(void *ptr, long long key) {
	if (!ptr) return 0;
	long long count = *(long long *)ptr;
	long long *pairs = (long long *)ptr + 2;
	for (long long i = 0; i < count; i++) {
		if (pairs[i * 2] == key)
			return pairs[i * 2 + 1];
	}
	return 0;
}

long long mira_dict_has(void *ptr, long long key) {
	if (!ptr) return 0;
	long long count = *(long long *)ptr;
	long long *pairs = (long long *)ptr + 2;
	for (long long i = 0; i < count; i++) {
		if (pairs[i * 2] == key)
			return 1;
	}
	return 0;
}

void mira_dict_free(void *ptr) {
	mem_free(ptr);
}

/* --- 字符串操作：str 为 char*，malloc 分配，用完后需 free --- */
long long mira_str_len(const char *s) {
	return s ? (long long)strlen(s) : 0;
}

char *mira_str_concat(const char *a, const char *b) {
	if (!a) a = "";
	if (!b) b = "";
	size_t la = strlen(a), lb = strlen(b);
	char *r = malloc(la + lb + 1);
	if (!r) return NULL;
	memcpy(r, a, la + 1);
	memcpy(r + la, b, lb + 1);
	return r;
}

char *mira_str_copy(const char *s) {
	if (!s) return NULL;
	size_t n = strlen(s) + 1;
	char *r = malloc(n);
	if (!r) return NULL;
	memcpy(r, s, n);
	return r;
}

/* --- 类型转换 --- */
char *mira_int_to_str(long long x) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%lld", x);
	return mira_str_copy(buf);
}

long long mira_str_to_int(const char *s) {
	if (!s) return 0;
	return (long long)strtoll(s, NULL, 10);
}

/* int -> float：返回 double 的 64 位位模式 */
long long mira_int_to_float(long long i) {
	union { long long ll; double d; } u;
	u.d = (double)i;
	return u.ll;
}

/* float -> int：输入 double 的 64 位位模式 */
long long mira_float_to_int(long long bits) {
	union { long long ll; double d; } u;
	u.ll = bits;
	return (long long)u.d;
}

/* --- 列表扩展：list-push 追加（需扩容），list-pop 取末 --- */
/* 简化版 list-push：list value -> 将 value 追加到末尾，内部 realloc */
void *mira_list_push(void *ptr, long long value) {
	if (!ptr) return NULL;
	long long *len = (long long *)ptr;
	long long n = *len;
	long long newcap = n + 1;
	void *p = realloc(ptr, 8 + newcap * 8);
	if (!p) return ptr;
	*(long long *)p = newcap;
	*(long long *)((char *)p + 8 + n * 8) = value;
	return p;
}

long long mira_list_pop(void *ptr) {
	if (!ptr) return 0;
	long long *len = (long long *)ptr;
	if (*len <= 0) return 0;
	(*len)--;
	return *(long long *)((char *)ptr + 8 + (*len) * 8);
}

/* --- 字典扩展：dict-keys 返回键列表（ptr），dict-count --- */
void *mira_dict_keys(void *ptr) {
	if (!ptr) return NULL;
	long long count = *(long long *)ptr;
	if (count <= 0) return mira_list_new(0);
	void *keys = mira_list_new(count);
	if (!keys) return NULL;
	long long *pairs = (long long *)ptr + 2;
	for (long long i = 0; i < count; i++)
		mira_list_set(keys, (int)i, pairs[i * 2]);
	return keys;
}

long long mira_dict_count(void *ptr) {
	return ptr ? *(long long *)ptr : 0;
}

/* --- 文件操作：path 为 char* --- */
char *mira_file_read(const char *path) {
	if (!path) return NULL;
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0) { fclose(f); return NULL; }
	char *buf = malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, (size_t)sz, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

long long mira_file_write(const char *path, const char *content) {
	if (!path) return -1;
	if (!content) content = "";
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	size_t len = strlen(content);
	if (len > 0 && fwrite(content, 1, len, f) != len) { fclose(f); return -1; }
	fclose(f);
	return 0;
}

long long mira_file_append(const char *path, const char *content) {
	if (!path) return -1;
	if (!content) content = "";
	FILE *f = fopen(path, "ab");
	if (!f) return -1;
	size_t len = strlen(content);
	if (fwrite(content, 1, len, f) != len) { fclose(f); return -1; }
	fclose(f);
	return 0;
}

long long mira_file_exists(const char *path) {
	if (!path) return 0;
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

/* 之前这里有 mira_async，用来直接从语言里启动外部进程。
   现在异步特性先整体移除，后续若实现 thread 库，可以在这里增加线程相关的 runtime 支持。 */
