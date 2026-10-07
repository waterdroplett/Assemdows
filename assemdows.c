/*
 * Assemdows - a small assembly-style language
 * Copyright (C) 2026 Your Name
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>

#define VERSION     "1.0.0"
#define MAX_PROG    4096
#define MAX_SYMS    1024
#define MAX_PENDING 32
#define MAX_WORDS   1024
#define NUM_REGS    16
#define MEM_SIZE    65536
#define STACK_SIZE  4096
#define MAX_LINE    512

/* Every instruction: name and operand signature.
   R = register, V = register or value, L = label, M = [memory], ? = optional */
#define OPLIST \
    X(MOV,"RV")  X(ADD,"RV")  X(SUB,"RV")  X(MUL,"RV")  X(DIV,"RV")  X(MOD,"RV") \
    X(AND,"RV")  X(OR,"RV")   X(XOR,"RV")  X(SHL,"RV")  X(SHR,"RV") \
    X(NEG,"R")   X(NOT,"R")   X(INC,"R")   X(DEC,"R") \
    X(CMP,"VV") \
    X(JMP,"L")   X(JE,"L")    X(JNE,"L")   X(JG,"L")    X(JL,"L") \
    X(JGE,"L")   X(JLE,"L")   X(CALL,"L")  X(RET,"") \
    X(PUSH,"V")  X(POP,"R") \
    X(LOAD,"RM") X(STORE,"MV") \
    X(PRINT,"V") X(PRINTC,"V") X(PRINTS,"V") X(INPUT,"R") X(GETC,"R") X(RAND,"RV") \
    X(NOP,"")    X(HALT,"?V")

enum {
#define X(n, s) OP_##n,
    OPLIST
#undef X
    NUM_OPS
};

static const struct { const char *name; const char *sig; } ops[] = {
#define X(n, s) { #n, s },
    OPLIST
#undef X
};

enum { K_NONE, K_REG, K_IMM, K_MEM };

typedef struct { int kind, reg, val; } Operand;

typedef struct {
    int op, line;
    Operand x, y;
    char *src;
    char *raw[4];
    int nraw;
} Instr;

typedef struct { char name[64]; int value, code; } Sym;

static const char *g_file = "";
static Instr prog[MAX_PROG];
static int   nprog;
static Sym   syms[MAX_SYMS];
static int   nsyms;
static int   mem[MEM_SIZE];
static int   dp;                       /* next free memory cell */
static char  pending[MAX_PENDING][64]; /* labels waiting for their address */
static int   pending_line[MAX_PENDING];
static int   npending;

/* ---------- errors ---------- */

static void fail(int line, const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s:%d: error: ", g_file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void rt_fail(const Instr *in, const char *fmt, ...) {
    va_list ap;
    fflush(stdout);
    if (in) fprintf(stderr, "%s:%d: runtime error: ", g_file, in->line);
    else    fprintf(stderr, "%s: runtime error: ", g_file);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (in) fprintf(stderr, "    %s\n", in->src);
    exit(1);
}

/* ---------- small helpers ---------- */

static int ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static char *trim(char *s) {
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* length of a character literal like 'A' or '\n' at p, or 0 */
static int charlit_len(const char *p) {
    if (p[0] != '\'') return 0;
    if (p[1] && p[1] != '\\' && p[2] == '\'') return 3;
    if (p[1] == '\\' && p[2] && p[3] == '\'') return 4;
    return 0;
}

static void strip_comment(char *s) {
    int in_str = 0, cl;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) s++;
            else if (*s == '"') in_str = 0;
        } else if (*s == '"') {
            in_str = 1;
        } else if ((cl = charlit_len(s)) != 0) {
            s += cl - 1;
        } else if (*s == ';') {
            *s = '\0';
            return;
        }
    }
}

static int unescape(char c, int line) {
    switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '0': return 0;
        case '\\': return '\\';
        case '"': return '"';
        case '\'': return '\'';
        default: fail(line, "unknown escape '\\%c'", c);
    }
    return 0;
}

/* Split "a, b, [R1+2]" on top-level commas. Destroys s. */
static int split_ops(char *s, char **out, int max, int line) {
    int n = 0, depth = 0, cl;
    char *start;
    s = trim(s);
    if (!*s) return 0;
    start = s;
    for (char *p = s;; p++) {
        if ((cl = charlit_len(p)) != 0) { p += cl - 1; continue; }
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        if ((*p == ',' && depth == 0) || *p == '\0') {
            char save = *p, *t;
            *p = '\0';
            t = trim(start);
            if (!*t) fail(line, "empty operand (stray comma?)");
            if (n >= max) fail(line, "too many operands");
            out[n++] = t;
            if (!save) break;
            start = p + 1;
        }
    }
    return n;
}

/* ---------- symbols ---------- */

static int parse_reg(const char *s) {
    int n;
    if (tolower((unsigned char)s[0]) != 'r' || !isdigit((unsigned char)s[1])) return -1;
    n = s[1] - '0';
    if (s[2] == '\0') return n;
    if (s[1] == '0' || !isdigit((unsigned char)s[2]) || s[3] != '\0') return -1;
    n = n * 10 + (s[2] - '0');
    return n < NUM_REGS ? n : -1;
}

static int valid_name(const char *s) {
    if (!(isalpha((unsigned char)*s) || *s == '_') || strlen(s) >= 64) return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_')) return 0;
    return 1;
}

static int find_sym(const char *n) {
    for (int i = 0; i < nsyms; i++)
        if (ieq(syms[i].name, n)) return i;
    return -1;
}

static void add_sym(const char *n, int value, int code, int line) {
    if (!valid_name(n)) fail(line, "'%s' is not a valid name", n);
    if (parse_reg(n) >= 0) fail(line, "'%s' is a register name and can't be used as a label", n);
    if (find_sym(n) >= 0) fail(line, "'%s' is already defined", n);
    if (nsyms >= MAX_SYMS) fail(line, "too many labels and constants");
    snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", n);
    syms[nsyms].value = value;
    syms[nsyms].code = code;
    nsyms++;
}

static void bind_pending(int addr, int code) {
    for (int i = 0; i < npending; i++) add_sym(pending[i], addr, code, pending_line[i]);
    npending = 0;
}

/* ---------- values and operands ---------- */

static int parse_number(const char *s, long long *out) {
    const char *p = s;
    char *end;
    int neg = 0, base = 10;
    long long v;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
    if (!isxdigit((unsigned char)*p)) return 0;
    v = strtoll(p, &end, base);
    if (*end) return 0;
    *out = neg ? -v : v;
    return 1;
}

static int eval_value(const char *s, int line) {
    long long v;
    int cl = charlit_len(s);
    if (cl && s[cl] == '\0') {
        v = (s[1] == '\\') ? unescape(s[2], line) : (unsigned char)s[1];
    } else if (parse_number(s, &v)) {
        /* ok */
    } else if (valid_name(s)) {
        int i = find_sym(s);
        if (i < 0) fail(line, "unknown name '%s'", s);
        v = syms[i].value;
    } else {
        fail(line, "can't understand '%s' as a number or name", s);
        return 0;
    }
    if (v < INT_MIN || v > INT_MAX) fail(line, "number '%s' is too big", s);
    return (int)v;
}

static void add_term(Operand *o, char *t, int sign, int line) {
    int r;
    if (!*t) fail(line, "missing value inside [ ]");
    r = parse_reg(t);
    if (r >= 0) {
        if (sign < 0) fail(line, "can't subtract a register inside [ ]");
        if (o->reg >= 0) fail(line, "only one register is allowed inside [ ]");
        o->reg = r;
    } else {
        o->val += sign * eval_value(t, line);
    }
}

static void parse_mem(const char *s, int line, Operand *o) {
    size_t n = strlen(s);
    char buf[128], *inner, *op, *right = NULL;
    int sign = 1;
    if (s[0] != '[' || s[n - 1] != ']')
        fail(line, "expected a memory operand like [R1] or [100], got '%s'", s);
    if (n < 3) fail(line, "empty memory operand []");
    if (n - 2 >= sizeof buf) fail(line, "memory operand is too long");
    memcpy(buf, s + 1, n - 2);
    buf[n - 2] = '\0';
    inner = trim(buf);
    o->kind = K_MEM; o->reg = -1; o->val = 0;
    for (op = inner + 1; *op; op++)
        if (*op == '+' || *op == '-') break;
    if (*op) {
        sign = (*op == '-') ? -1 : 1;
        *op = '\0';
        right = trim(op + 1);
    }
    add_term(o, trim(inner), 1, line);
    if (right) add_term(o, right, sign, line);
}

static void parse_operand(char kind, const char *s, int line, Operand *o) {
    int r;
    switch (kind) {
    case 'R':
        r = parse_reg(s);
        if (r < 0) fail(line, "expected a register (R0-R%d), got '%s'", NUM_REGS - 1, s);
        o->kind = K_REG; o->reg = r;
        break;
    case 'V':
        if (s[0] == '[')
            fail(line, "memory operands like %s only work with LOAD and STORE", s);
        r = parse_reg(s);
        if (r >= 0) { o->kind = K_REG; o->reg = r; }
        else        { o->kind = K_IMM; o->val = eval_value(s, line); }
        break;
    case 'L': {
        int i;
        if (!valid_name(s) || parse_reg(s) >= 0) fail(line, "expected a label, got '%s'", s);
        i = find_sym(s);
        if (i < 0) fail(line, "unknown label '%s'", s);
        if (!syms[i].code) fail(line, "'%s' is data or a constant, not a code label", s);
        o->kind = K_IMM; o->val = syms[i].value;
        break;
    }
    case 'M':
        parse_mem(s, line, o);
        break;
    }
}

/* ---------- reading the program ---------- */

static void mem_put(int v, int line) {
    if (dp >= MEM_SIZE) fail(line, "data doesn't fit in memory (%d cells)", MEM_SIZE);
    mem[dp++] = v;
}

static void directive(char *name, char *rest, int line) {
    if (ieq(name, ".equ")) {
        char *parts[4], *nm, *val;
        int n = split_ops(rest, parts, 4, line);
        if (n == 2) { nm = parts[0]; val = parts[1]; }
        else if (n == 1) {
            char *q = nm = parts[0];
            while (*q && !isspace((unsigned char)*q)) q++;
            if (!*q) fail(line, ".equ needs a name and a value, like: .equ SIZE, 10");
            *q++ = '\0';
            val = trim(q);
        } else {
            fail(line, ".equ needs a name and a value, like: .equ SIZE, 10");
            return;
        }
        add_sym(nm, eval_value(val, line), 0, line);
    } else if (ieq(name, ".word")) {
        char *parts[MAX_WORDS];
        int n = split_ops(rest, parts, MAX_WORDS, line);
        if (n == 0) fail(line, ".word needs at least one value");
        bind_pending(dp, 0);
        for (int i = 0; i < n; i++) mem_put(eval_value(parts[i], line), line);
    } else if (ieq(name, ".string")) {
        char *p = rest;
        if (*p != '"') fail(line, ".string needs text in double quotes, like: .string \"Hello\"");
        p++;
        bind_pending(dp, 0);
        while (*p && *p != '"') {
            int c;
            if (*p == '\\') {
                p++;
                if (!*p) break;
                c = unescape(*p, line);
            } else {
                c = (unsigned char)*p;
            }
            mem_put(c, line);
            p++;
        }
        if (*p != '"') fail(line, "string is missing its closing quote");
        p++;
        if (*trim(p)) fail(line, "unexpected text after the string");
        mem_put(0, line);
    } else if (ieq(name, ".space")) {
        int n;
        if (!*rest) fail(line, ".space needs a size, like: .space 10");
        n = eval_value(rest, line);
        if (n < 0) fail(line, ".space size can't be negative");
        bind_pending(dp, 0);
        for (int i = 0; i < n; i++) mem_put(0, line);
    } else {
        fail(line, "unknown directive '%s' (known: .equ .word .string .space)", name);
    }
}

static void parse_line(char *line, int lineno) {
    char *s, *m, *rest, *stmt;
    strip_comment(line);
    s = trim(line);
    if (!*s) return;

    /* any number of "label:" prefixes */
    for (;;) {
        char *p = s;
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        if (p > s && *p == ':') {
            size_t len = (size_t)(p - s);
            if (len >= sizeof pending[0]) fail(lineno, "label name is too long");
            if (npending >= MAX_PENDING) fail(lineno, "too many labels in a row");
            memcpy(pending[npending], s, len);
            pending[npending][len] = '\0';
            pending_line[npending++] = lineno;
            s = trim(p + 1);
            if (!*s) return;
        } else {
            break;
        }
    }

    stmt = s;
    m = s;
    while (*s && !isspace((unsigned char)*s)) s++;
    if (*s) *s++ = '\0';
    rest = trim(s);

    if (m[0] == '.') {
        directive(m, rest, lineno);
        return;
    }

    {
        int op = -1;
        Instr *in;
        char *parts[4];
        int n;
        for (int i = 0; i < NUM_OPS; i++)
            if (ieq(ops[i].name, m)) { op = i; break; }
        if (op < 0) fail(lineno, "unknown instruction '%s'", m);
        if (nprog >= MAX_PROG) fail(lineno, "program is too long (max %d instructions)", MAX_PROG);

        in = &prog[nprog];
        memset(in, 0, sizeof *in);
        in->op = op;
        in->line = lineno;
        n = split_ops(rest, parts, 4, lineno);
        {   /* rebuild a clean copy of the statement for error messages and --trace */
            char text[MAX_LINE + 8];
            size_t len;
            if (n > 0) snprintf(text, sizeof text, "%s %s", m, parts[0]);
            else       snprintf(text, sizeof text, "%s", m);
            len = strlen(text);
            for (int i = 1; i < n && len < sizeof text - 4; i++)
                len += (size_t)snprintf(text + len, sizeof text - len, ", %s", parts[i]);
            in->src = strdup(text);
        }
        (void)stmt;
        for (int i = 0; i < n; i++) in->raw[i] = strdup(parts[i]);
        in->nraw = n;

        bind_pending(nprog, 1);
        nprog++;
    }
}

static void resolve(void) {
    for (int i = 0; i < nprog; i++) {
        Instr *in = &prog[i];
        const char *sig = ops[in->op].sig;
        int opt = (sig[0] == '?');
        const char *letters = sig + opt;
        int max = (int)strlen(letters), min = max - opt;

        if (in->nraw < min || in->nraw > max) {
            if (min == max)
                fail(in->line, "%s takes %d operand%s, but %d %s given", ops[in->op].name,
                     max, max == 1 ? "" : "s", in->nraw, in->nraw == 1 ? "was" : "were");
            else
                fail(in->line, "%s takes %d or %d operands, but %d were given",
                     ops[in->op].name, min, max, in->nraw);
        }
        for (int j = 0; j < in->nraw; j++) {
            parse_operand(letters[j], in->raw[j], in->line, j == 0 ? &in->x : &in->y);
            free(in->raw[j]);
            in->raw[j] = NULL;
        }
    }
}

static void load_program(const char *path) {
    FILE *f = fopen(path, "r");
    char line[MAX_LINE + 2];
    int lineno = 0;
    if (!f) {
        fprintf(stderr, "assemdows: can't open '%s'\n", path);
        exit(1);
    }
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        lineno++;
        if (len > 0 && line[len - 1] != '\n' && !feof(f))
            fail(lineno, "line is too long (max %d characters)", MAX_LINE);
        if (lineno == 1 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, strlen(line + 3) + 1);   /* skip UTF-8 BOM */
        parse_line(line, lineno);
    }
    fclose(f);
    bind_pending(nprog, 1);
    if (nprog == 0) fail(lineno ? lineno : 1, "the program has no instructions");
    resolve();
}

/* ---------- running ---------- */

static uint32_t rng_state = 1;
static uint32_t rng(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* wrapping arithmetic, so overflow never causes undefined behaviour */
static int wadd(int a, int b) { return (int)((unsigned)a + (unsigned)b); }
static int wsub(int a, int b) { return (int)((unsigned)a - (unsigned)b); }
static int wmul(int a, int b) { return (int)((unsigned)a * (unsigned)b); }

static int mem_addr(const Instr *in, const Operand *o, const int *regs) {
    long long a = (long long)(o->reg >= 0 ? regs[o->reg] : 0) + o->val;
    if (a < 0 || a >= MEM_SIZE)
        rt_fail(in, "memory address %lld is out of range (0-%d)", a, MEM_SIZE - 1);
    return (int)a;
}

static int run(int trace, int dump, long long maxsteps) {
    int regs[NUM_REGS] = {0};
    static int stack[STACK_SIZE];
    int sp = 0, flag = 0, pc = 0;
    long long steps = 0;

#define VAL(o) ((o).kind == K_REG ? regs[(o).reg] : (o).val)
#define DST    regs[in->x.reg]

    for (;;) {
        const Instr *in;
        if (pc < 0 || pc >= nprog)
            rt_fail(NULL, "ran past the end of the program (did you forget HALT?)");
        in = &prog[pc];
        if (trace) fprintf(stderr, "[%4d] line %-4d %s\n", pc, in->line, in->src);
        if (maxsteps && ++steps > maxsteps)
            rt_fail(in, "step limit of %lld reached (infinite loop?)", maxsteps);
        pc++;

        switch (in->op) {
        case OP_MOV: DST = VAL(in->y); break;
        case OP_ADD: DST = wadd(DST, VAL(in->y)); break;
        case OP_SUB: DST = wsub(DST, VAL(in->y)); break;
        case OP_MUL: DST = wmul(DST, VAL(in->y)); break;
        case OP_DIV: {
            int b = VAL(in->y);
            if (b == 0) rt_fail(in, "division by zero");
            DST = (b == -1) ? wsub(0, DST) : DST / b;
            break;
        }
        case OP_MOD: {
            int b = VAL(in->y);
            if (b == 0) rt_fail(in, "division by zero");
            DST = (b == -1) ? 0 : DST % b;
            break;
        }
        case OP_AND: DST &= VAL(in->y); break;
        case OP_OR:  DST |= VAL(in->y); break;
        case OP_XOR: DST ^= VAL(in->y); break;
        case OP_SHL: DST = (int)((unsigned)DST << (VAL(in->y) & 31)); break;
        case OP_SHR: DST = DST >> (VAL(in->y) & 31); break;
        case OP_NEG: DST = wsub(0, DST); break;
        case OP_NOT: DST = ~DST; break;
        case OP_INC: DST = wadd(DST, 1); break;
        case OP_DEC: DST = wsub(DST, 1); break;

        case OP_CMP: {
            int a = VAL(in->x), b = VAL(in->y);
            flag = (a > b) - (a < b);
            break;
        }
        case OP_JMP: pc = in->x.val; break;
        case OP_JE:  if (flag == 0) pc = in->x.val; break;
        case OP_JNE: if (flag != 0) pc = in->x.val; break;
        case OP_JG:  if (flag > 0)  pc = in->x.val; break;
        case OP_JL:  if (flag < 0)  pc = in->x.val; break;
        case OP_JGE: if (flag >= 0) pc = in->x.val; break;
        case OP_JLE: if (flag <= 0) pc = in->x.val; break;

        case OP_CALL:
            if (sp >= STACK_SIZE) rt_fail(in, "stack overflow (too many nested CALLs or PUSHes)");
            stack[sp++] = pc;
            pc = in->x.val;
            break;
        case OP_RET:
            if (sp <= 0) rt_fail(in, "RET with an empty stack");
            pc = stack[--sp];
            if (pc < 0 || pc > nprog)
                rt_fail(in, "RET jumped to an invalid address (unbalanced PUSH and POP?)");
            break;
        case OP_PUSH:
            if (sp >= STACK_SIZE) rt_fail(in, "stack overflow (too many nested CALLs or PUSHes)");
            stack[sp++] = VAL(in->x);
            break;
        case OP_POP:
            if (sp <= 0) rt_fail(in, "POP with an empty stack");
            DST = stack[--sp];
            break;

        case OP_LOAD:  DST = mem[mem_addr(in, &in->y, regs)]; break;
        case OP_STORE: mem[mem_addr(in, &in->x, regs)] = VAL(in->y); break;

        case OP_PRINT:  printf("%d\n", VAL(in->x)); break;
        case OP_PRINTC: putchar(VAL(in->x) & 255); break;
        case OP_PRINTS: {
            int a = VAL(in->x);
            for (;;) {
                if (a < 0 || a >= MEM_SIZE)
                    rt_fail(in, "PRINTS ran off the end of memory (string has no end?)");
                if (mem[a] == 0) break;
                putchar(mem[a] & 255);
                a++;
            }
            break;
        }
        case OP_INPUT: {
            int v;
            fflush(stdout);
            if (scanf("%d", &v) != 1) rt_fail(in, "INPUT expected a whole number");
            DST = v;
            break;
        }
        case OP_GETC:
            fflush(stdout);
            DST = getchar();   /* -1 at end of input */
            break;
        case OP_RAND: {
            int max = VAL(in->y);
            if (max <= 0) rt_fail(in, "RAND needs a maximum above 0");
            DST = (int)(rng() % (uint32_t)max);
            break;
        }

        case OP_NOP: break;
        case OP_HALT: {
            int code = (in->x.kind == K_NONE) ? 0 : VAL(in->x);
            if (dump) {
                fprintf(stderr, "--- registers at HALT ---\n");
                for (int i = 0; i < NUM_REGS; i++)
                    fprintf(stderr, "R%-2d = %-11d%s", i, regs[i], (i % 4 == 3) ? "\n" : "  ");
            }
            return code;
        }
        }
    }
#undef VAL
#undef DST
}

/* ---------- command line ---------- */

static void usage(void) {
    printf("Assemdows %s\n\n"
           "Usage: assemdows [options] program.asdw\n\n"
           "Options:\n"
           "  --trace          print every instruction as it runs (to stderr)\n"
           "  --regs           print all registers when the program halts\n"
           "  --seed N         set the random seed (same seed = same RAND results)\n"
           "  --max-steps N    stop with an error after N instructions (catches infinite loops)\n"
           "  -v, --version    show the version\n"
           "  -h, --help       show this help\n", VERSION);
}

int main(int argc, char **argv) {
    const char *path = NULL;
    int trace = 0, dump = 0, code;
    long long maxsteps = 0;
    uint32_t seed = (uint32_t)time(NULL);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--trace")) trace = 1;
        else if (!strcmp(a, "--regs")) dump = 1;
        else if (!strcmp(a, "--seed") && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--max-steps") && i + 1 < argc) maxsteps = strtoll(argv[++i], NULL, 10);
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("Assemdows %s\n", VERSION); return 0; }
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-') { fprintf(stderr, "assemdows: unknown option '%s' (try --help)\n", a); return 1; }
        else if (path) { fprintf(stderr, "assemdows: only one program file at a time\n"); return 1; }
        else path = a;
    }
    if (!path) { usage(); return 1; }

    rng_state = seed ? seed : 1;
    g_file = path;
    load_program(path);
    code = run(trace, dump, maxsteps);
    fflush(stdout);
    return code;
}