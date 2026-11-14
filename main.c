// ── main.c — Lua 5.5 on NeoRunST (单编译单元移植) ──
// 构建: buildnep neorunst-lua\main.c dev\tools\math\*.c -o lua.nep
// (需 NeoRunST dev 工具链: buildnep + dev\tools 头文件, 内核需支持
//  syscall 43 堆扩容 / 44 TICKS)
// 目录: lua\ = Lua 5.5.1 官方源码 (原样, 未修改); main.c = 移植层
// 移植层:
//   - 内存: heap.h (syscall 43) → lua_Alloc
//   - 控制台: putchar 行缓冲 → lua_writestring; nep_read_line → REPL/读
//   - 文件: dev\stdio.h 的 fopen 家族 (AA:// 路径) → io 库
//   - os.time = uptime 秒 (无 RTC); os.clock = nep_ticks 毫秒
//   - strtod 自实现 (10/16 进制, inf/nan) — llex/luaO_str2d 依赖

#include "nep.h"
#include "stdio.h"
#include "string.h"
#include "math.h"
#include "ctype.h"
#include "stdlib.h"      // heap.h (malloc 家族) + exit/abort/getenv shim

// ══════════════════ 控制台 stdout/stderr/stdin ══════════════════
// fd 约定: -2 = 控制台输入, -3/-4 = 控制台输出 (内核无控制台 fd,
// 控制台走 putchar / nep_read_line; 负 fd 让 stdio 的真文件函数跳过)

static __attribute__((section(".data"))) FILE nr_stdin_file  = { -2, 0, 0, -1 };
static __attribute__((section(".data"))) FILE nr_stdout_file = { -3, 0, 0, -1 };
static __attribute__((section(".data"))) FILE nr_stderr_file = { -4, 0, 0, -1 };

#define stdin  (&nr_stdin_file)
#define stdout (&nr_stdout_file)
#define stderr (&nr_stderr_file)

// ── 控制台写 (经 putchar 行缓冲; '\n' 立即落屏) ──
static void nr_lua_wstr(const char* s, unsigned long l) {
    for (unsigned long i = 0; i < l; i++) putchar((unsigned char)s[i]);
}

// 错误消息 (stderr): 同一缓冲保证与 stdout 输出顺序一致
static void nr_lua_werr(const char* fmt, const char* arg) {
    char buf[400];
    snprintf(buf, sizeof(buf), fmt, arg);
    nr_lua_wstr(buf, strlen(buf));
    putchar('\n');
}

// 截获 fwrite: 控制台 fd → putchar; 真文件 → stdio.h 原版
#define fwrite(s, z, n, f) nr_lua_fwrite(s, z, n, f)
static unsigned long nr_lua_fwrite(const void* s, unsigned long z,
                                   unsigned long n, FILE* f) {
    if (f && f->fd < 0) {
        const char* p = (const char*)s;
        unsigned long total = z * n;
        for (unsigned long i = 0; i < total; i++) putchar((unsigned char)p[i]);
        return n;
    }
    return (fwrite)(s, z, n, f);
}

// ── 控制台 stdin 行馈送: nep_read_line 一次一行, fgetc/fgets/fread 消费 ──
static __attribute__((section(".data"))) char nr_inbuf[260];
static __attribute__((section(".data"))) int  nr_inlen = 0;
static __attribute__((section(".data"))) int  nr_inpos = 0;

static int nr_in_fill(void) {
    long n = nep_read_line(nr_inbuf, (long)(sizeof(nr_inbuf) - 2));
    if (n < 0) return 0;
    nr_inbuf[n] = '\n';                       // fgets 语义: 行以 '\n' 结尾
    nr_inlen = (int)n + 1;
    nr_inpos = 0;
    return 1;
}

static int nr_lua_fgetc(FILE* f) {
    if (f && f->fd < 0) {
        if (f->ungot >= 0) { int c = f->ungot; f->ungot = -1; return c; }
        if (nr_inpos >= nr_inlen && !nr_in_fill()) return EOF;
        return (unsigned char)nr_inbuf[nr_inpos++];
    }
    return (fgetc)(f);
}
#define fgetc(f) nr_lua_fgetc(f)

static int nr_lua_ungetc(int c, FILE* f) {
    if (f && f->fd < 0) {
        if (c == EOF || f->ungot >= 0) return EOF;
        f->ungot = (unsigned char)c;
        f->eof = 0;
        return c;
    }
    return (ungetc)(c, f);
}
#define ungetc(c, f) nr_lua_ungetc(c, f)

static char* nr_lua_fgets(char* buf, int n, FILE* f) {
    if (!f || !buf || n < 1) return NULL;
    if (n == 1) { buf[0] = '\0'; return buf; }
    int i = 0;
    while (i < n - 1) {
        int c = nr_lua_fgetc(f);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}
#define fgets(b, n, f) nr_lua_fgets(b, n, f)

static unsigned long nr_lua_fread(void* p, unsigned long z,
                                  unsigned long n, FILE* f) {
    if (!p || z == 0 || n == 0) return 0;
    if (f && f->fd < 0) {
        unsigned char* d = (unsigned char*)p;
        unsigned long total = z * n;
        if (total / n != z) return 0;
        unsigned long got = 0;
        while (got < total) {
            int c = nr_lua_fgetc(f);
            if (c == EOF) break;
            d[got++] = (unsigned char)c;
        }
        return got / z;
    }
    return (fread)(p, z, n, f);
}
#define fread(p, z, n, f) nr_lua_fread(p, z, n, f)

// ══════════════════ llimits.h 输出钩子 ══════════════════
#define lua_writestring(s, l)      nr_lua_wstr((s), (unsigned long)(l))
#define lua_writeline()            nr_lua_wstr("\n", 1)
#define lua_writestringerror(s, p) nr_lua_werr((s), (p))

// ══════════════════ strtod (10/16 进制 + inf/nan) ══════════════════
// 数字解析核心: llex 的 lua_strx2number / luaO_str2d / tostring 回读全走它
double strtod(const char* s, char** end) {
    const char* p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    // inf / infinity
    if (p[0] && p[1] && p[2] &&
        (p[0]|32) == 'i' && (p[1]|32) == 'n' && (p[2]|32) == 'f') {
        p += 3;
        if (p[0] && p[1] && p[2] && p[3] &&
            (p[0]|32) == 'i' && (p[1]|32) == 'n' && (p[2]|32) == 'y') p += 3;
        if (end) *end = (char*)p;
        return neg ? -INFINITY : INFINITY;
    }
    // nan
    if (p[0] && p[1] && p[2] &&
        (p[0]|32) == 'n' && (p[1]|32) == 'a' && (p[2]|32) == 'n') {
        if (end) *end = (char*)(p + 3);
        return __builtin_nan("");
    }

    int hex = 0;
    if (p[0] == '0' && (p[1]|32) == 'x' &&
        (isxdigit((unsigned char)p[2]) || p[2] == '.')) {
        hex = 1; p += 2;
    }

    unsigned long long mant = 0;
    int ndig = 0, seen_dot = 0, any = 0;
    int exp10 = 0, exp2 = 0;

    while (1) {
        unsigned char c = (unsigned char)*p;
        int v;
        if (hex) {
            if (c >= '0' && c <= '9') v = c - '0';
            else if ((c|32) >= 'a' && (c|32) <= 'f') v = (c|32) - 'a' + 10;
            else break;
            any = 1;
            if (ndig < 16) { mant = (mant << 4) | (unsigned)v; ndig++; }
            if (seen_dot) exp2 -= 4;
            p++;
        } else {
            if (c < '0' || c > '9') {
                if (c != '.' || seen_dot) break;
                seen_dot = 1; p++;
                continue;
            }
            any = 1;
            v = c - '0';
            if (ndig < 19 && mant < 1844674407370955161ULL) { mant = mant * 10 + (unsigned)v; ndig++; }
            else if (!seen_dot) exp10++;      // 丢掉的多余高位数字按数量级补偿
            if (seen_dot) exp10--;
            p++;
        }
    }
    if (!any) {
        if (end) *end = (char*)s;
        return 0.0;
    }

    if (hex) {
        const char* q = p + 1;
        if (q[0] == '+' || q[0] == '-') q++;
        if ((p[0]|32) == 'p' && q[0] >= '0' && q[0] <= '9') {   // 2 的幂指数
            int sg = 0, e = 0;
            p++;
            if (p[0] == '+' || p[0] == '-') { sg = (p[0] == '-'); p++; }
            while (*p >= '0' && *p <= '9') {
                if (e < 10000) e = e * 10 + (*p - '0');
                p++;
            }
            exp2 += sg ? -e : e;
        }
        if (end) *end = (char*)p;
        double v = ldexp((double)mant, exp2);
        return neg ? -v : v;
    }

    const char* qe = p + 1;
    if (qe[0] == '+' || qe[0] == '-') qe++;
    if ((p[0]|32) == 'e' && qe[0] >= '0' && qe[0] <= '9') {   // 10 的幂指数
        int sg = 0;
        long long e = 0;
        p++;
        if (p[0] == '+' || p[0] == '-') { sg = (p[0] == '-'); p++; }
        while (*p >= '0' && *p <= '9') {
            if (e < 100000) e = e * 10 + (*p - '0');
            p++;
        }
        exp10 += (int)(sg ? -e : e);
    }

    double v;
    if (mant == 0) v = 0.0;
    else if (exp10 == 0) v = (double)mant;
    else v = (double)mant * pow(10.0, (double)exp10);
    if (end) *end = (char*)p;
    return neg ? -v : v;
}

// ══════════════════ Lua 本体 ══════════════════
#define MAKE_LIB
#include "lua/onelua.c"

// ══════════════════ 顶层: 脚本 / REPL ══════════════════

static lua_State* gL = NULL;
static __attribute__((section(".data"))) char nr_chunk[8192];
static unsigned int nr_clen = 0;              // REPL 未完成 chunk 长度

// pcall 错误处理器: 附加 traceback
static int nr_msgh(lua_State* L) {
    if (!lua_isstring(L, 1)) return 1;
    luaL_traceback(L, L, lua_tostring(L, 1), 0);
    return 1;
}

static int nr_script(const char* path) {
    // 无盘符 → 默认 AA://
    char real[256];
    if (!strchr(path, ':')) {
        if (strlen(path) > sizeof(real) - 8) { printf("path too long\n"); return -1; }
        strcpy(real, "AA://");
        strcat(real, path);
        path = real;
    }
    FILE* f = fopen(path, "rb");
    if (!f) { printf("lua: cannot open %s\n", path); return -1; }
    static __attribute__((section(".data"))) char src[65536];
    unsigned long n = fread(src, 1, sizeof(src) - 1, f);
    int ferr = ferror(f);
    fclose(f);
    if (n == 0 && ferr) { printf("lua: read error %s\n", path); return -1; }
    src[n] = '\0';

    char name[280];
    snprintf(name, sizeof(name), "@%s", path);
    int lstatus = luaL_loadbuffer(gL, src, n, name);
    if (lstatus != LUA_OK) {
        const char* msg = lua_tostring(gL, -1);
        printf("lua: %s\n", msg ? msg : "(syntax error)");
        lua_pop(gL, 1);
        return -1;
    }
    // 压错误处理器后执行
    lua_pushcfunction(gL, nr_msgh);
    lua_insert(gL, 1);
    int status = lua_pcall(gL, 0, LUA_MULTRET, 1);
    lua_remove(gL, 1);
    if (status != LUA_OK) {
        const char* msg = lua_tostring(gL, -1);
        printf("lua: %s\n", msg ? msg : "(error object is not a string)");
        lua_pop(gL, 1);
    }
    lua_settop(gL, 0);
    return (status == LUA_OK) ? 0 : -1;
}

static void nr_repl(void) {
    printf("%s\n", LUA_COPYRIGHT);
    nr_clen = 0;
    for (;;) {
        printf("%s", nr_clen ? "... " : "lua> ");
        console_flush();
        char line[256];
        long n = nep_read_line(line, (long)sizeof(line) - 2);
        if (n < 0) break;
        if (nr_clen + (unsigned long)n + 1 >= sizeof(nr_chunk) - 1) {
            printf("lua: chunk too long\n");
            nr_clen = 0;
            continue;
        }
        memcpy(nr_chunk + nr_clen, line, (unsigned long)n);
        nr_clen += (unsigned long)n;
        nr_chunk[nr_clen++] = '\n';
        nr_chunk[nr_clen] = '\0';

        int status = luaL_loadbuffer(gL, nr_chunk, nr_clen, "=stdin");
        if (status == LUA_ERRSYNTAX) {
            const char* msg = lua_tostring(gL, -1);
            unsigned long ml = msg ? strlen(msg) : 0;
            if (ml >= 6 && memcmp(msg + ml - 6, "<eof>", 6) == 0) {
                lua_pop(gL, 1);               // 不完整 → 继续续行
                continue;
            }
        }
        if (status != LUA_OK) {
            const char* msg = lua_tostring(gL, -1);
            printf("lua: %s\n", msg ? msg : "(syntax error)");
            lua_pop(gL, 1);
            nr_clen = 0;
            continue;
        }
        // 压错误处理器, 执行
        lua_pushcfunction(gL, nr_msgh);
        lua_insert(gL, 1);
        status = lua_pcall(gL, 0, LUA_MULTRET, 1);
        lua_remove(gL, 1);
        if (status != LUA_OK) {
            const char* msg = lua_tostring(gL, -1);
            printf("lua: %s\n", msg ? msg : "(error)");
            lua_pop(gL, 1);
        } else {
            int nres = lua_gettop(gL);
            if (nres > 0) {                   // 用 print 渲染返回值
                lua_getglobal(gL, "print");
                lua_insert(gL, 1);
                if (lua_pcall(gL, nres, 0, 0) != LUA_OK) {
                    const char* msg = lua_tostring(gL, -1);
                    printf("lua: %s\n", msg ? msg : "(error)");
                    lua_pop(gL, 1);
                }
            }
        }
        lua_settop(gL, 0);
        nr_clen = 0;
    }
}

int nep_main(void) {
    gL = luaL_newstate();
    if (!gL) { printf("lua: cannot create state\n"); return 1; }
    luaL_openlibs(gL);

    // 全局 arg 表: arg[-1]=解释器 arg[0]=脚本 arg[1..]=参数
    int argc = 0;
    char** argv = nep_get_args(&argc);
    lua_createtable(gL, argc > 0 ? argc - 1 : 0, 1);
    if (argv && argc > 0) {
        lua_pushstring(gL, argv[0]);
        lua_rawseti(gL, -2, -1);
        for (int i = 1; i < argc; i++) {
            lua_pushstring(gL, argv[i]);
            lua_rawseti(gL, -2, i - 1);
        }
    }
    lua_setglobal(gL, "arg");

    if (argc > 1 && argv) return nr_script(argv[1]);
    nr_repl();
    return 0;
}
