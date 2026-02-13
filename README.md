# Mira 1.0

栈式 / 后置语法的脚本语言，编译为 x64 汇编，运行时调用 C 实现的库函数。

## 构建与运行

**依赖**：`gcc`、`nasm`（需在 PATH 中）。构建时使用 `gcc -Wall -Wextra` 以尽早发现潜在错误。

```bash
build.bat
example.exe
```

## 已修复问题（v0.9.1）

- **浮点数 print** 和 **文件操作** 崩溃 — 根因是 `mira_main` 中栈未 16 字节对齐（`push rbp` + `push r12` 后 `rsp % 16 == 8`），导致 SSE 指令崩溃。已在 `codegen/program.c` 中添加 `sub rsp, 8` 修复。

## 语法概览

Mira 采用**后置**语法：操作数在前，操作符在后，运算从栈上取值。例如 `3 4 +` 表示 3+4，`a b +` 表示 a+b。

## 模块与导入

```mira
# 导入其他 .mira 文件（路径相对于当前文件所在目录）
import "utils.mira"
import "lib/math.mira"

# 被导入文件中的定义（函数、变量、常量）可直接使用
```

- `import "path"` 或 `import path`：在解析前将目标文件内容展开，支持递归导入
- 已导入过的文件不会重复加载（避免循环引用）

## 定义与调用

### 函数

```mira
greet: { "Hello!" print }
add: { a b } a b +

greet
3 4 add print
```

- 无参：`name: { body }`
- 有参：`name: { params } body`，参数名用空格分隔

### 变量

```mira
x: 42           # 定义并赋值
y: "hello"      # 字符串
x @             # 取 x 的值（@ 从地址加载）
x @ 1 + x !     # x = x + 1（! 写入地址）
```

## 控制流

### if

```mira
# 有 else
age @ 18 >= { "成年" print } { "未成年" print } if

# 无 else
age @ 18 > { "You've grown up!" print } if
```

### for

```mira
# 索引压栈（body 内用 print 打印当前索引）
0 5 for {
    print
}

# Python 风格：变量可修改
i 0 5 for {
    i @ print
    i @ 1 + i !   # 可修改 i
}
```

### while

```mira
i: 0
{ i @ 5 < } {
    i @ print
    i @ 1 + i !
} while
```

### switch

```mira
# 有 default
x @ 1 { "one" print } 2 { "two" print } default { "other" print } switch

# 无 default
x @ 1 { "one" print } 2 { "two" print } switch
```

- 每个 case 后默认 `break`，只执行匹配分支
- 支持 `break`、`continue`（在循环中）

## 数据结构

### 列表

```mira
# 字面量
d: 4[1 2 3 4]
d @ 0 list-get print

# 手动构造
lst: 3 list-new
10 lst @ 0 list-set
lst @ 0 list-get print
lst @ list-free
```

### 字典

```mira
m: 4 dict-new
m @ 1 100 dict-set
m @ 1 dict-get print
m @ 1 dict-has print
m @ dict-count print
m @ dict-keys list-free
m @ dict-free
```

### 字符串操作

```mira
"hello" str-len print
"hello" " world" str-concat print
s: 0
"hello" str-copy s !
s @ print
s @ free
42 int->str print
"123" str->int 1 + print
```

### 文件操作

```mira
"hello.txt" "content" file-write print
"hello.txt" file-exists print
content: 0
"hello.txt" file-read content !
content @ print
content @ free
"hello.txt" " more" file-append print
```

### 类型转换

```mira
42 int->str " is answer" str-concat print
3 int->float 2.0 f* print   # float 运算正常，但 print 会崩溃
3.14 float->int print       # float->int 后可正常 print
```

## 输入输出

| 词 | 说明 |
|----|------|
| `print` | 弹出栈顶并打印（支持 int / string / bool；**float 暂不可用**） |
| `read` | 从 stdin 读一个整数，压栈 |
| `input` | 从 stdin 读一行字符串，压栈 |

```mira
"Your name:" print
name: 0
input name !
"Hello " print
name @ print
```

## 常用内建词

| 词 | 说明 |
|----|------|
| `+` `-` `*` `/` `%` | 算术 |
| `@` `!` | 取地址处的值 / 写入地址 |
| `return` | 从当前函数返回，栈顶为返回值 |
| `allocate` `free` | 内存分配 / 释放 |
| `list-new` `list-get` `list-set` `list-len` `list-free` | 列表操作 |
| `list-push` `list-pop` | 列表追加 / 取末（list-push 可能返回新指针，需写回变量） |
| `dict-new` `dict-set` `dict-get` `dict-has` `dict-free` | 字典操作 |
| `dict-keys` `dict-count` | 字典键列表 / 键数量 |
| `str-len` `str-concat` `str-copy` | 字符串长度 / 拼接 / 复制 |
| `file-read` `file-write` `file-append` `file-exists` | 文件读 / 写 / 追加 / 存在检查（**当前会崩溃**，待修复） |
| `int->str` `str->int` `int->float` `float->int` | 类型转换 |
| `break` `continue` | 跳出循环 / 进入下一轮 |

## 注释

`#` 至行尾为注释。
