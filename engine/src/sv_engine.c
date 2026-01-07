// sv_engine.c (assembly-only: arm64 + x86_64)
#include "sv_engine.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

static char OUT[65536];
static size_t out_len = 0;

static SV_Sim SIM;

typedef struct {
    uint64_t addr;
    uint64_t value;
    char label[32];
    bool used;
} MemCell;

#define MEM_MAX 512
static MemCell MEM[MEM_MAX];

static void mem_reset(void) { memset(MEM, 0, sizeof(MEM)); }

static MemCell* mem_find(uint64_t addr) {
    for (int i = 0; i < MEM_MAX; i++) {
        if (MEM[i].used && MEM[i].addr == addr) return &MEM[i];
    }
    return NULL;
}

static MemCell* mem_upsert(uint64_t addr) {
    MemCell* c = mem_find(addr);
    if (c) return c;
    for (int i = 0; i < MEM_MAX; i++) {
        if (!MEM[i].used) {
            MEM[i].used = true;
            MEM[i].addr = addr;
            MEM[i].value = 0;
            MEM[i].label[0] = '\0';
            return &MEM[i];
        }
    }
    return NULL;
}

static bool mem_read_u64(uint64_t addr, uint64_t* out) {
    MemCell* c = mem_find(addr);
    if (!c) return false;
    if (out) *out = c->value;
    return true;
}

/* ---------------------------
   helpers
   --------------------------- */

static char* ltrim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return s;
}

static void rtrim_inplace(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) {
        s[n-1] = 0;
        n--;
    }
}

static bool is_blank_or_comment(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    return (*s == 0 || *s == '\n' || *s == '#');
}

static char* next_tok(char** p) {
    char* s = *p;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == 0) { *p = s; return NULL; }

    if (*s == '"') {
        s++;
        char* start = s;
        while (*s && *s != '"') s++;
        if (*s == '"') { *s = 0; s++; }
        *p = s;
        return start;
    }

    char* start = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
    if (*s) { *s = 0; s++; }
    *p = s;
    return start;
}

static bool parse_i64(const char* t, int64_t* out) {
    if (!t) return false;
    char* end = NULL;
    long long v = strtoll(t, &end, 0);
    if (!end) return false;

    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != 0 && *end != ']' && *end != ',') return false;

    if (out) *out = (int64_t)v;
    return true;
}

static bool parse_u64(const char* t, uint64_t* out) {
    if (!t) return false;
    char* end = NULL;
    unsigned long long v = strtoull(t, &end, 0);
    if (!end) return false;

    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != 0 && *end != ']' && *end != ',') return false;

    if (out) *out = (uint64_t)v;
    return true;
}

static bool parse_u64_hash_imm(const char* t, uint64_t* out) {
    if (!t) return false;
    if (t[0] == '#') t++;
    return parse_u64(t, out);
}

static uint64_t align_down_u64(uint64_t x, uint64_t a) {
    return (a == 0) ? x : (x / a) * a;
}

static uint64_t align_up_u64(uint64_t x, uint64_t a) {
    if (a == 0) return x;
    uint64_t r = x % a;
    return r == 0 ? x : (x + (a - r));
}

/* sanitize token: trims spaces and strips trailing commas/brackets */
static void sanitize_token_inplace(char* s) {
    if (!s) return;

    // trim leading
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    // trim trailing spaces
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) { s[n-1] = 0; n--; }

    // strip trailing punctuation that can cling to tokens
    n = strlen(s);
    while (n > 0 && (s[n-1] == ',' || s[n-1] == ']')) { s[n-1] = 0; n--; }
}

/* robust parse: [x29, #-4] / [x29,#-4] / [x29, -4] / [x29] */
static int64_t parse_arm64_bracket_off(const char* s, bool* ok) {
    if (ok) *ok = false;
    if (!s) return 0;

    const char* lb = strchr(s, '[');
    const char* rb = strchr(s, ']');
    if (!lb || !rb || rb < lb) return 0;

    // look for comma inside bracket
    const char* comma = NULL;
    for (const char* p = lb; p < rb; p++) {
        if (*p == ',') { comma = p; break; }
    }

    if (!comma) {
        if (ok) *ok = true;
        return 0;
    }

    const char* p = comma + 1;
    while (p < rb && isspace((unsigned char)*p)) p++;

    if (p < rb && *p == '#') p++;
    while (p < rb && isspace((unsigned char)*p)) p++;

    char buf[64];
    size_t j = 0;
    while (p < rb && j + 1 < sizeof(buf)) {
        if (isspace((unsigned char)*p)) break;
        buf[j++] = *p++;
    }
    buf[j] = 0;
    sanitize_token_inplace(buf);

    int64_t off = 0;
    if (!parse_i64(buf, &off)) return 0;

    if (ok) *ok = true;
    return off;
}

/* ---------------------------
   sim core
   --------------------------- */

static void sim_set_error(const char* msg) {
    SIM.ok = false;
    snprintf(SIM.msg, sizeof(SIM.msg), "%s", msg ? msg : "error");
}

static void sim_set_ok(const char* msg) {
    SIM.ok = true;
    snprintf(SIM.msg, sizeof(SIM.msg), "%s", msg ? msg : "ok");
}

/* ---------------------------
   register map for asm parsing + per-step snapshot
   --------------------------- */
typedef struct {
    char name[32];
    uint64_t value;
    bool used;
} RegCell;

#define REG_MAX 128
static RegCell REGS[REG_MAX];
static RegCell STEP_REGS[MAX_STEPS][REG_MAX];

static void regs_reset(void) {
    memset(REGS, 0, sizeof(REGS));
    memset(STEP_REGS, 0, sizeof(STEP_REGS));
}

static RegCell* reg_find(const char* name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < REG_MAX; i++) {
        if (REGS[i].used && strcmp(REGS[i].name, name) == 0) return &REGS[i];
    }
    return NULL;
}

static RegCell* reg_upsert(const char* name) {
    RegCell* r = reg_find(name);
    if (r) return r;
    for (int i = 0; i < REG_MAX; i++) {
        if (!REGS[i].used) {
            REGS[i].used = true;
            snprintf(REGS[i].name, sizeof(REGS[i].name), "%s", name);
            REGS[i].value = 0;
            return &REGS[i];
        }
    }
    return NULL;
}

static bool reg_get(const char* name, uint64_t* out) {
    RegCell* r = reg_find(name);
    if (!r) return false;
    if (out) *out = r->value;
    return true;
}

static void reg_set(const char* name, uint64_t v) {
    RegCell* r = reg_upsert(name);
    if (r) r->value = v;
}

static void regs_snapshot_for_step(size_t step_i) {
    if (step_i >= MAX_STEPS) return;
    memcpy(STEP_REGS[step_i], REGS, sizeof(REGS));
}

static void sim_reset(void) {
    memset(&SIM, 0, sizeof(SIM));
    mem_reset();
    regs_reset();
    sim_set_ok("ok");
}

static void sim_set_regs(uint64_t rip, uint64_t rsp, uint64_t rbp) {
    SIM.regs.rip = rip;
    SIM.regs.rsp = rsp;
    SIM.regs.rbp = rbp;
}

static SV_Step* sim_new_step(const char* instr) {
    if (SIM.step_count >= MAX_STEPS) return NULL;

    SV_Step* s = &SIM.steps[SIM.step_count];
    memset(s, 0, sizeof(*s));
    snprintf(s->instr, MAX_INSTR_LEN, "%s", instr ? instr : "");
    s->regs = SIM.regs;
    s->write_count = 0;

    regs_snapshot_for_step(SIM.step_count);

    SIM.step_count++;
    return s;
}

static SV_Step* sim_get_step(const char* instr, uint64_t rip, uint64_t rsp, uint64_t rbp) {
    sim_set_regs(rip, rsp, rbp);
    return sim_new_step(instr);
}

static int step_add_write_abs(SV_Step* s, uint64_t addr_abs, uint64_t value, const char* label) {
    if (!s) return 0;
    if (s->write_count >= MAX_WRITES_PER_STEP) return 0;

    SV_Write* w = &s->writes[s->write_count++];
    w->addr = addr_abs;
    w->value = value;

    if (label && label[0] != '\0') snprintf(w->label, sizeof(w->label), "%s", label);
    else w->label[0] = '\0';

    MemCell* c = mem_upsert(addr_abs);
    if (c) {
        c->value = value;
        if (label && label[0]) snprintf(c->label, sizeof(c->label), "%s", label);
        else c->label[0] = '\0';
    }
    return 1;
}

static int step_add_write_off(SV_Step* s, uint64_t rbp, int64_t off, uint64_t value, const char* label) {
    uint64_t addr_abs = (uint64_t)((int64_t)rbp + off);
    return step_add_write_abs(s, addr_abs, value, label);
}

/* ---------------------------
   json output (per-step memory history + regs snapshot)
   --------------------------- */

static void out_reset(void) { out_len = 0; OUT[0] = '\0'; }

static int out_append(const char* fmt, ...) {
    if (out_len >= sizeof(OUT)) return 0;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(OUT + out_len, sizeof(OUT) - out_len, fmt, args);
    va_end(args);

    if (n < 0) return 0;
    if ((size_t)n >= sizeof(OUT) - out_len) {
        out_len = sizeof(OUT) - 1;
        OUT[out_len] = '\0';
        return 0;
    }

    out_len += (size_t)n;
    return 1;
}

static void json_escape_into(char* dst, size_t dst_cap, const char* src) {
    size_t j = 0;
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src) src = "";

    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\\' || c == '"') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else { dst[j++] = (char)c; }
    }
    dst[j] = '\0';
}

typedef struct {
    bool found;
    uint64_t value;
    char label[32];
} HistCell;

static HistCell hist_latest_write_up_to(size_t step_i, uint64_t addr_abs) {
    HistCell hc;
    hc.found = false;
    hc.value = 0;
    hc.label[0] = '\0';

    for (size_t i = 0; i <= step_i && i < SIM.step_count; i++) {
        SV_Step* s = &SIM.steps[i];
        for (size_t w = 0; w < s->write_count; w++) {
            SV_Write* wr = &s->writes[w];
            if (wr->addr == addr_abs) {
                hc.found = true;
                hc.value = wr->value;
                snprintf(hc.label, sizeof(hc.label), "%s", wr->label);
            }
        }
    }
    return hc;
}

static void sim_to_json(int syntax, size_t input_len) {
    out_reset();

    out_append("{");
    out_append("\"ok\":%s,", SIM.ok ? "true" : "false");
    out_append("\"syntax\":%d,", syntax);
    out_append("\"input_len\":%zu,", input_len);

    char msg_esc[256];
    json_escape_into(msg_esc, sizeof(msg_esc), SIM.msg[0] ? SIM.msg : "ok");
    out_append("\"message\":\"%s\",", msg_esc);

    out_append("\"step_count\":%zu,", SIM.step_count);
    out_append("\"steps\":[");

    for (size_t i = 0; i < SIM.step_count; i++) {
        SV_Step* s = &SIM.steps[i];
        if (i > 0) out_append(",");

        char instr_esc[256];
        json_escape_into(instr_esc, sizeof(instr_esc), s->instr);

        out_append("{");
        out_append("\"instr\":\"%s\",", instr_esc);

        out_append("\"frame\":{");
        out_append("\"base\":\"rbp\",");
        out_append("\"top\":\"0x%llx\",", (unsigned long long)s->regs.rbp);
        out_append("\"bottom\":\"0x%llx\"", (unsigned long long)s->regs.rsp);
        out_append("},");

        out_append("\"regs\":{");
        out_append("\"rip\":\"0x%llx\",", (unsigned long long)s->regs.rip);
        out_append("\"rsp\":\"0x%llx\",", (unsigned long long)s->regs.rsp);
        out_append("\"rbp\":\"0x%llx\"",  (unsigned long long)s->regs.rbp);
        out_append("},");

        out_append("\"gprs\":[");
        int first_g = 1;
        for (int r = 0; r < REG_MAX; r++) {
            if (!STEP_REGS[i][r].used) continue;
            if (!first_g) out_append(",");
            first_g = 0;
            char name_esc[64];
            json_escape_into(name_esc, sizeof(name_esc), STEP_REGS[i][r].name);
            out_append("{\"name\":\"%s\",\"value\":\"0x%llx\"}",
                       name_esc, (unsigned long long)STEP_REGS[i][r].value);
        }
        out_append("],");

        out_append("\"writes\":[");
        for (size_t w = 0; w < s->write_count; w++) {
            SV_Write* wr = &s->writes[w];
            if (w > 0) out_append(",");

            int64_t off = (int64_t)wr->addr - (int64_t)s->regs.rbp;

            char label_esc[128];
            json_escape_into(label_esc, sizeof(label_esc), wr->label);

            out_append("{");
            out_append("\"base\":\"rbp\",");
            out_append("\"off\":%lld,", (long long)off);
            out_append("\"addr\":\"0x%llx\",", (unsigned long long)wr->addr);
            out_append("\"value\":\"0x%llx\",", (unsigned long long)wr->value);
            out_append("\"label\":\"%s\"", label_esc);
            out_append("}");
        }
        out_append("],");

        {
            const uint64_t SLOT = 4;

            uint64_t min_addr = s->regs.rsp;
            uint64_t max_addr = s->regs.rbp;

            for (size_t si = 0; si <= i; si++) {
                SV_Step* ss = &SIM.steps[si];
                for (size_t w = 0; w < ss->write_count; w++) {
                    uint64_t a = ss->writes[w].addr;
                    if (a < min_addr) min_addr = a;
                    if (a > max_addr) max_addr = a;
                }
            }

            min_addr = align_down_u64(min_addr, SLOT);
            max_addr = align_up_u64(max_addr, SLOT);

            out_append("\"slots\":[");
            size_t slot_i = 0;

            for (uint64_t a = max_addr;; a -= SLOT) {
                if (slot_i++ > 0) out_append(",");

                int64_t off = (int64_t)a - (int64_t)s->regs.rbp;

                out_append("{");
                out_append("\"addr\":\"0x%llx\",", (unsigned long long)a);
                out_append("\"off\":%lld,", (long long)off);
                out_append("\"is_sp\":%s,", (a == s->regs.rsp) ? "true" : "false");
                out_append("\"is_fp\":%s", (a == s->regs.rbp) ? "true" : "false");

                HistCell hc = hist_latest_write_up_to(i, a);
                if (hc.found) {
                    char lab_esc[64];
                    json_escape_into(lab_esc, sizeof(lab_esc), hc.label);
                    out_append(",\"value\":\"0x%llx\"", (unsigned long long)hc.value);
                    if (hc.label[0]) out_append(",\"label\":\"%s\"", lab_esc);
                }

                out_append("}");

                if (a == min_addr) break;
            }

            out_append("]");
        }

        out_append("}");
    }

    out_append("]");
    out_append("}");
}

/* ---------------------------
   ARM64 helpers
   --------------------------- */

static bool parse_arm64_str_src_reg(const char* line, char* out_reg, size_t cap) {
    if (!line || !out_reg || cap == 0) return false;
    out_reg[0] = '\0';

    const char* p = line;

    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "str", 3) != 0) return false;
    p += 3;

    while (*p && isspace((unsigned char)*p)) p++;

    size_t j = 0;
    while (*p && *p != ',' && !isspace((unsigned char)*p) && j + 1 < cap) {
        out_reg[j++] = *p++;
    }
    out_reg[j] = '\0';
    sanitize_token_inplace(out_reg);

    return out_reg[0] != '\0';
}

/* ---------------------------
   x86_64 helpers
   - supports: mov dword ptr [rbp-4], 5
   --------------------------- */
static bool parse_x64_mem_rbp_off_and_imm(const char* line, int64_t* out_off, uint64_t* out_imm) {
    if (out_off) *out_off = 0;
    if (out_imm) *out_imm = 0;
    if (!line) return false;

    // find "[rbp"
    const char* b = strstr(line, "[rbp");
    if (!b) return false;

    // find closing bracket
    const char* rb = strchr(b, ']');
    if (!rb) return false;

    // parse optional + / - inside brackets
    // patterns: [rbp-4] [rbp - 4] [rbp+0x10]
    int64_t off = 0;
    {
        const char* p = b + 4; // after "[rbp"
        while (p < rb && isspace((unsigned char)*p)) p++;

        if (p < rb && (*p == '+' || *p == '-')) {
            char sign = *p++;
            while (p < rb && isspace((unsigned char)*p)) p++;

            char numbuf[64];
            size_t j = 0;
            while (p < rb && j + 1 < sizeof(numbuf)) {
                if (isspace((unsigned char)*p) || *p == ']') break;
                numbuf[j++] = *p++;
            }
            numbuf[j] = 0;
            sanitize_token_inplace(numbuf);

            int64_t v = 0;
            if (!parse_i64(numbuf, &v)) return false;
            off = (sign == '-') ? -v : v;
        } else {
            off = 0;
        }
    }

    // parse immediate after comma
    const char* comma = strchr(rb, ',');
    if (!comma) return false;

    const char* p = comma + 1;
    while (*p && isspace((unsigned char)*p)) p++;

    // accept 0x.. or decimal
    char ibuf[64];
    size_t k = 0;
    while (*p && k + 1 < sizeof(ibuf)) {
        if (*p == '\n' || *p == '\r') break;
        ibuf[k++] = *p++;
    }
    ibuf[k] = 0;
    sanitize_token_inplace(ibuf);

    uint64_t imm = 0;
    if (!parse_u64(ibuf, &imm)) return false;

    if (out_off) *out_off = off;
    if (out_imm) *out_imm = imm;
    return true;
}

/* ---------------------------
   detect architecture from first non-empty line
   --------------------------- */
static bool detect_arm64(const char* input) {
    if (!input) return true;
    char tmp[256];
    size_t n = strlen(input);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, input, n);
    tmp[n] = 0;

    char* save = NULL;
    for (char* line = strtok_r(tmp, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* p = ltrim(line);
        rtrim_inplace(p);
        if (is_blank_or_comment(p)) continue;

        // arm64 usually starts with stp/mov/sub/str using x29/sp
        if (strncmp(p, "stp ", 4) == 0) return true;
        if (strncmp(p, "mov ", 4) == 0 && (strstr(p, "x29") || strstr(p, "sp"))) return true;
        if (strncmp(p, "str ", 4) == 0 && strstr(p, "[x29")) return true;

        // x64 typical: push rbp / mov rbp, rsp
        if (strncmp(p, "push ", 5) == 0) return false;
        if (strncmp(p, "mov ", 4) == 0 && (strstr(p, "rbp") || strstr(p, "rsp"))) return false;

        // default: assume arm64 (your project default)
        return true;
    }

    return true;
}

/* ---------------------------
   simulate entrypoint (syntax 0 = assembly only)
   --------------------------- */
const char* simulate(const char* input, int syntax) {
    sim_reset();

    size_t n = input ? strlen(input) : 0;
    if (n > 2000) n = 2000;

    if (syntax != 0) {
        sim_set_error("unsupported syntax");
        sim_to_json(syntax, n);
        return OUT;
    }

    sim_set_ok("ok");

    bool is_arm = detect_arm64(input);

    uint64_t rip = is_arm ? 0x108760 : 0x401000;
    uint64_t rip_step = is_arm ? 4 : 1;

    // keep stack addresses consistent with your UI demos
    uint64_t rsp = 0x7fffffffd000;
    uint64_t rbp = 0x7fffffffd020;

    SV_Step* cur = sim_get_step("entry", rip, rsp, rbp);
    if (!cur) { sim_set_error("no room for steps"); sim_to_json(syntax, n); return OUT; }
    rip += rip_step;

    char buf[2048 + 1];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, input ? input : "", n);

    char* save = NULL;
    size_t line_no = 0;

    for (char* line = strtok_r(buf, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {

        line_no++;

        char* p = ltrim(line);
        rtrim_inplace(p);
        if (is_blank_or_comment(p)) continue;

        char instr_line[256];
        snprintf(instr_line, sizeof(instr_line), "%s", p);

        if (is_arm) {
            /* ---------------- ARM64 subset ---------------- */

            if (strncmp(p, "stp ", 4) == 0) {
                // Only: stp x29, x30, [sp, #-16]!
                uint64_t old_x29 = rbp;
                uint64_t old_x30 = 0;
                reg_get("x30", &old_x30);

                rsp -= 16;
                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }

                step_add_write_abs(cur, rsp, old_x29, "saved_fp");
                step_add_write_abs(cur, rsp + 8, old_x30, "saved_lr");

                rip += rip_step;
                continue;
            }

            if (strncmp(p, "mov ", 4) == 0) {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s", p);
                for (size_t k = 0; tmp[k]; k++) if (tmp[k] == ',') tmp[k] = ' ';

                char* it = tmp;
                next_tok(&it); // mov
                char* dst = next_tok(&it);
                char* src = next_tok(&it);

                if (!dst || !src) { sim_set_error("bad mov"); break; }

                sanitize_token_inplace(dst);
                sanitize_token_inplace(src);

                if (strcmp(dst, "x29") == 0 && strcmp(src, "sp") == 0) {
                    rbp = rsp;
                    cur = sim_get_step(instr_line, rip, rsp, rbp);
                    if (!cur) { sim_set_error("no room for steps"); break; }
                    rip += rip_step;
                    continue;
                }

                uint64_t imm = 0;
                if (!parse_u64_hash_imm(src, &imm)) {
                    sim_set_error("mov supports immediates like: mov w0, #5");
                    break;
                }

                reg_set(dst, imm);

                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            if (strncmp(p, "sub ", 4) == 0) {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s", p);
                for (size_t k = 0; tmp[k]; k++) if (tmp[k] == ',') tmp[k] = ' ';

                char* it = tmp;
                next_tok(&it); // sub
                char* dst = next_tok(&it);
                char* src1 = next_tok(&it);
                char* imm_t = next_tok(&it);

                if (!dst || !src1 || !imm_t) { sim_set_error("bad sub"); break; }

                sanitize_token_inplace(dst);
                sanitize_token_inplace(src1);
                sanitize_token_inplace(imm_t);

                if (strcmp(dst, "sp") == 0 && strcmp(src1, "sp") == 0) {
                    uint64_t imm = 0;
                    if (!parse_u64_hash_imm(imm_t, &imm)) {
                        sim_set_error("sub sp, sp needs immediate like #0x20");
                        break;
                    }
                    rsp -= imm;
                    cur = sim_get_step(instr_line, rip, rsp, rbp);
                    if (!cur) { sim_set_error("no room for steps"); break; }
                    rip += rip_step;
                    continue;
                }

                sim_set_error("only supports: sub sp, sp, #imm");
                break;
            }

            if (strncmp(p, "str ", 4) == 0) {
                char src_reg[32];
                if (!parse_arm64_str_src_reg(p, src_reg, sizeof(src_reg))) {
                    sim_set_error("bad str (expected: str wN, [x29, #off])");
                    break;
                }

                const char* br = strchr(p, '[');
                if (!br) { sim_set_error("bad str address"); break; }

                bool ok_off = false;
                int64_t off = parse_arm64_bracket_off(br, &ok_off);
                if (!ok_off) { sim_set_error("bad str address (expected [x29, #off])"); break; }

                uint64_t v = 0;
                if (!reg_get(src_reg, &v)) v = 0;

                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }

                step_add_write_off(cur, rbp, off, v, src_reg);

                rip += rip_step;
                continue;
            }

            if (strcmp(p, "ret") == 0) {
                cur = sim_get_step("ret", rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            {
                char msg[160];
                snprintf(msg, sizeof(msg), "unsupported arm64 on line %zu: %s", line_no, p);
                sim_set_error(msg);
                break;
            }
        } else {
            /* ---------------- x86_64 subset ---------------- */

            // push rbp
            if (strcmp(p, "push rbp") == 0 || strcmp(p, "push\t%rbp") == 0) {
                rsp -= 8;
                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                step_add_write_abs(cur, rsp, rbp, "saved_rbp");
                rip += rip_step;
                continue;
            }

            // mov rbp, rsp
            if (strcmp(p, "mov rbp, rsp") == 0 || strcmp(p, "mov rbp,rsp") == 0) {
                rbp = rsp;
                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            // sub rsp, imm
            if (strncmp(p, "sub rsp", 7) == 0) {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "%s", p);
                for (size_t k = 0; tmp[k]; k++) if (tmp[k] == ',') tmp[k] = ' ';

                char* it = tmp;
                next_tok(&it); // sub
                char* dst = next_tok(&it);
                char* imm_t = next_tok(&it);

                if (!dst || !imm_t) { sim_set_error("bad sub"); break; }
                sanitize_token_inplace(dst);
                sanitize_token_inplace(imm_t);

                if (strcmp(dst, "rsp") != 0) { sim_set_error("only supports: sub rsp, imm"); break; }

                uint64_t imm = 0;
                if (!parse_u64(imm_t, &imm)) { sim_set_error("sub rsp needs imm"); break; }

                rsp -= imm;
                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            // mov dword ptr [rbp-4], imm
            if (strncmp(p, "mov ", 4) == 0 && strstr(p, "[rbp") && strchr(p, ',')) {
                int64_t off = 0;
                uint64_t imm = 0;
                if (!parse_x64_mem_rbp_off_and_imm(p, &off, &imm)) {
                    char msg[180];
                    snprintf(msg, sizeof(msg), "unsupported mov mem on line %zu: %s", line_no, p);
                    sim_set_error(msg);
                    break;
                }

                cur = sim_get_step(instr_line, rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }

                char lab[32];
                snprintf(lab, sizeof(lab), "slot_%lld", (long long)off);

                step_add_write_off(cur, rbp, off, imm, lab);

                rip += rip_step;
                continue;
            }

            // leave
            if (strcmp(p, "leave") == 0) {
                // mov rsp, rbp
                rsp = rbp;
                cur = sim_get_step("leave", rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;

                // pop rbp
                {
                    uint64_t saved = 0;
                    if (mem_read_u64(rsp, &saved)) rbp = saved;
                    rsp += 8;
                }

                cur = sim_get_step("leave_pop", rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            // ret
            if (strcmp(p, "ret") == 0) {
                cur = sim_get_step("ret", rip, rsp, rbp);
                if (!cur) { sim_set_error("no room for steps"); break; }
                rip += rip_step;
                continue;
            }

            {
                char msg[160];
                snprintf(msg, sizeof(msg), "unsupported x86_64 on line %zu: %s", line_no, p);
                sim_set_error(msg);
                break;
            }
        }

        if (!SIM.ok) break;
    }

    sim_to_json(syntax, n);
    return OUT;
}
