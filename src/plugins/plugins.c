/* The plugin runtime (sub-slice 8b): see include/exhaust_plugins.h and docs/formats.md (The runtime). One thread runs every plugin's Lua state; the interface
 * thread only posts jobs and answers the plugins' calls into the application once a frame. */
#include "exhaust_plugins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "exhaust.h"
#include "exhaust_ini.h"
#include "exhaust_platform.h"
#include "exhaust_worker.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define LOAD_LIMIT_MS 1000
#define CALL_LIMIT_MS 1000
#define MEM_LIMIT (32u * 1024u * 1024u)
#define MAX_READ (8u * 1024u * 1024u)
#define MAX_WRITE (16u * 1024u * 1024u)
#define UI_WAIT_MS 10000
#define MAX_EVENTS_QUEUED 64
#define MAX_CMDS 200
#define MAX_EVS 64
#define MAX_PANES 32
#define MAX_PLUGINS 64

enum { MSG_REQ = 1, MSG_CHANGED = 2 };
enum { R_EDITOR_GET = 1, R_EDITOR_REPLACE, R_NOTICE, R_OPEN_PANE, R_RENAME, R_CMD_ADD, R_CMD_RUN, R_CMDS_REMOVE };
enum { J_SCAN = 1, J_ENABLE, J_CMD, J_EVENT, J_STOP };

typedef struct Req {
    int kind;
    char *s1, *s2, *s3, *s4;
    char *text; size_t len;
    int i1, rc, nowait;
    size_t n0, n1, n2;
    SDL_AtomicInt state;          /* 0 waiting, 1 answered, 2 given up by the plugin */
    SDL_Semaphore *sem;
} Req;

typedef struct Job { int kind; char *a, *b, *c; int i1; struct Job *next; } Job;

typedef struct Mem { size_t used, limit; int exceeded; } Mem;
typedef struct CmdRef { char id[64]; int ref, editor; } CmdRef;
typedef struct EvRef { char name[24]; int ref; } EvRef;
typedef struct PaneRef { char id[64], title[120]; char *text; int render_ref; } PaneRef;

typedef struct Plug {
    ExPluginInfo info;
    char folder[1400];
    lua_State *L;
    Mem mem;
    CmdRef cmds[MAX_CMDS]; size_t ncmds;
    EvRef evs[MAX_EVS]; size_t nevs;
    PaneRef panes[MAX_PANES]; size_t npanes;
    uint64_t start_ms, waited_ms, limit_ms;
    int active, over;
    struct ExPlugins *P;
} Plug;

typedef struct StoppedRec { char id[64]; char error[200]; } StoppedRec;

struct ExPlugins {
    char root[1200], dir[1400], ini_path[1400];
    ExCommands *commands;
    ExPluginHost host;
    ExQueue *queue;
    void (*wake)(void *); void *wake_user;
    SDL_Thread *thread;
    SDL_Mutex *mu; SDL_Condition *cv;
    Job *jobs, *jobs_tail; int njobs, running_job, stop;
    SDL_AtomicInt allowed, done;
    uint64_t last_editor_event;
    ExPluginInfo infos[MAX_PLUGINS]; size_t ninfos;          /* the snapshot (under mu) */
    Plug *plugs[MAX_PLUGINS]; size_t nplugs;                 /* the thread's own */
    StoppedRec stopped[MAX_PLUGINS]; size_t nstopped;
    char **cmd_users; size_t ncmd_users, capcmd_users;       /* trampolines of the commands added (interface thread) */
};

static char *dup_s(const char *s) { char *d = malloc(strlen(s ? s : "") + 1); if (d) strcpy(d, s ? s : ""); return d; }

/* ---- memory limit ---- */

static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    Mem *m = ud;
    if (!ptr) osize = 0;
    if (nsize == 0) { if (ptr) { free(ptr); m->used -= osize; } return NULL; }
    if (m->used - osize + nsize > m->limit) { m->exceeded = 1; return NULL; }
    {   void *np = realloc(ptr, nsize);
        if (np) m->used += nsize - osize;
        return np; }
}

/* ---- the plugin thread's side of a call into the application ---- */

static Plug *plug_of(lua_State *L) { return *(Plug **)lua_getextraspace(L); }

static void free_req(Req *r) { free(r->s1); free(r->s2); free(r->s3); free(r->s4); free(r->text); if (r->sem) SDL_DestroySemaphore(r->sem); free(r); }

static void post_req(ExPlugins *P, Req *r) {
    ExMessage m;
    memset(&m, 0, sizeof m);
    m.type = MSG_REQ; m.payload = r;
    if (ex_queue_push(P->queue, &m) != 0) { free_req(r); return; }
    if (P->wake) P->wake(P->wake_user);
}

/* a call that waits for the answer: 0 answered (r is the caller's to free), -1 the application did not answer (r now belongs to the interface thread) */
static int call_ui(Plug *p, Req *r) {
    uint64_t t0 = ex_platform_now_ms();
    int ok;
    r->sem = SDL_CreateSemaphore(0);
    if (!r->sem) { free_req(r); return -1; }
    SDL_SetAtomicInt(&r->state, 0);
    post_req(p->P, r);
    ok = SDL_WaitSemaphoreTimeout(r->sem, UI_WAIT_MS);
    p->waited_ms += ex_platform_now_ms() - t0;
    if (!ok && SDL_CompareAndSwapAtomicInt(&r->state, 0, 2)) return -1;
    return 0;
}

static void send_notice(Plug *p, const char *fmt, const char *a) {
    Req *r = calloc(1, sizeof *r);
    char t[400];
    if (!r) return;
    snprintf(t, sizeof t, fmt, p->info.name, a ? a : "");
    r->kind = R_NOTICE; r->s1 = dup_s(t); r->nowait = 1;
    post_req(p->P, r);
}

static void log_line(Plug *p, const char *text) {
    char path[1500];
    char *old;
    size_t len = 0, tl = strlen(text);
    snprintf(path, sizeof path, "%s/log.txt", p->folder);
    old = ex_platform_read_file(path, &len);
    {
        char *nb = malloc(len + tl + 2);
        size_t keep = len, off = 0;
        if (!nb) { free(old); return; }
        if (keep + tl + 1 > 64 * 1024) { off = keep + tl + 1 - 32 * 1024; if (off > keep) off = keep; }
        if (old) memcpy(nb, old + off, keep - off);
        memcpy(nb + keep - off, text, tl);
        nb[keep - off + tl] = '\n';
        ex_platform_write_file_atomic(path, nb, keep - off + tl + 1);
        free(nb);
    }
    free(old);
}

/* ---- the time limit and the sandbox ---- */

static void hook(lua_State *L, lua_Debug *ar) {
    Plug *p = plug_of(L);
    (void)ar;
    if (!p || !p->active) return;
    if (p->over || ex_platform_now_ms() - p->start_ms - p->waited_ms > p->limit_ms) { p->over = 1; luaL_error(L, "time limit exceeded"); }
}

static int l_pcall(lua_State *L) {
    Plug *p = plug_of(L);
    int st;
    luaL_checkany(L, 1);
    lua_pushboolean(L, 1);
    lua_insert(L, 1);
    st = lua_pcall(L, lua_gettop(L) - 2, LUA_MULTRET, 0);
    if (p && (p->over || p->mem.exceeded) && st != LUA_OK) return lua_error(L);          /* a limit cannot be caught */
    if (st != LUA_OK) { lua_pushboolean(L, 0); lua_insert(L, -2); return 2; }
    return lua_gettop(L);
}

static int l_xpcall(lua_State *L) {
    Plug *p = plug_of(L);
    int n = lua_gettop(L), st;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushboolean(L, 1);
    lua_pushvalue(L, 1);
    lua_rotate(L, 3, 2);
    st = lua_pcall(L, n - 2, LUA_MULTRET, 2);
    if (p && (p->over || p->mem.exceeded) && st != LUA_OK) return lua_error(L);
    if (st != LUA_OK) { lua_pushboolean(L, 0); lua_pushvalue(L, -2); return 2; }
    return lua_gettop(L) - 2;
}

static int l_resume(lua_State *L) {                    /* coroutine.resume that cannot swallow a limit */
    Plug *p = plug_of(L);
    int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    if (p && (p->over || p->mem.exceeded)) return luaL_error(L, p->over ? "time limit exceeded" : "not enough memory");
    return lua_gettop(L);
}

static int l_print(lua_State *L) {
    Plug *p = plug_of(L);
    int i, n = lua_gettop(L);
    char line[800];
    size_t w = 0;
    for (i = 1; i <= n && w < sizeof line - 2; i++) {
        size_t l;
        const char *s = luaL_tolstring(L, i, &l);
        if (i > 1) line[w++] = '\t';
        if (l > sizeof line - w - 2) l = sizeof line - w - 2;
        memcpy(line + w, s, l); w += l;
        lua_pop(L, 1);
    }
    line[w] = 0;
    if (p) log_line(p, line);
    return 0;
}

static int has_perm(const Plug *p, const char *perm) {
    size_t i;
    for (i = 0; i < p->info.nperm; i++) if (!strcmp(p->info.permissions[i], perm)) return 1;
    return 0;
}
static void need(lua_State *L, const char *perm) {
    Plug *p = plug_of(L);
    if (!p || !has_perm(p, perm)) luaL_error(L, "permission denied: %s", perm);
}

/* ---- paths ---- */

static int safe_rel(const char *in, char *out, size_t cap, int allow_empty) {
    size_t i, n = in ? strlen(in) : 0;
    const char *part;
    if (n == 0) { if (!allow_empty) return 0; out[0] = 0; return 1; }
    if (n >= cap || n > 500) return 0;
    if (in[0] == '/' || strchr(in, '\\') || (n >= 2 && in[1] == ':')) return 0;
    for (i = 0; i < n; i++) if ((unsigned char)in[i] < 32 || in[i] == 127) return 0;
    for (part = in; *part;) {
        size_t pl = strcspn(part, "/");
        if (pl == 0) return 0;                                                   /* an empty part: "a//b" or a trailing slash */
        if ((pl == 1 && part[0] == '.') || (pl == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (part[pl - 1] == '.' || part[pl - 1] == ' ') return 0;                /* Windows drops a trailing dot or space: ".exhaust." is ".exhaust" there */
        if (part == in) {
            if (pl == 8 && !SDL_strncasecmp(part, ".exhaust", 8)) return 0;
            if (pl == 6 && !SDL_strncasecmp(part, ".trash", 6)) return 0;
        }
        part += pl;
        if (*part == '/') part++;
    }
    snprintf(out, cap, "%s", in);
    return 1;
}

/* Whether what the path leads to, with every symbolic link followed, is inside the vault and not in .exhaust or .trash.
 * A path that does not exist yet is judged by its deepest folder that does (a write makes the rest). */
static int inside_vault(const ExPlugins *P, const char *abs) {
    char *root = ex_platform_realpath(P->root), *real = NULL, *probe = NULL;
    size_t rl;
    int ok = 0;
    if (!root) return 0;
    probe = dup_s(abs);
    if (!probe) { free(root); return 0; }
    for (;;) {                                               /* shorten the path until a part of it exists */
        real = ex_platform_realpath(probe);
        if (real) break;
        {
            char *sl = strrchr(probe, '/');
            if (!sl || sl == probe) break;
            *sl = 0;
        }
    }
    if (!real) goto done;
    rl = strlen(root);                                       /* what does not exist yet holds no links, and safe_rel vetted its names */
    if (strncmp(real, root, rl) != 0) goto done;
    if (real[rl] != 0 && real[rl] != '/') goto done;       /* "/vault2" is not inside "/vault" */
    {
        const char *rest = real + rl;
        if (*rest == '/') rest++;
        if ((!SDL_strncasecmp(rest, ".exhaust", 8) && (rest[8] == 0 || rest[8] == '/')) || (!SDL_strncasecmp(rest, ".trash", 6) && (rest[6] == 0 || rest[6] == '/'))) goto done;
    }
    ok = 1;
done:
    free(root); free(real); free(probe);
    return ok;
}

static char *abs_path(const ExPlugins *P, const char *rel, char *buf, size_t cap) { snprintf(buf, cap, "%s%s%s", P->root, rel[0] ? "/" : "", rel); return buf; }

/* ---- the API ---- */

static int l_version(lua_State *L) { lua_pushstring(L, exhaust_version()); lua_pushinteger(L, 1); return 2; }

static const char *path_arg(lua_State *L, int idx, char *buf, size_t cap, int allow_empty) {
    const char *s = luaL_checkstring(L, idx);
    if (!safe_rel(s, buf, cap, allow_empty)) luaL_error(L, "path not allowed: %s", s);
    return buf;
}

static int l_vault_read(lua_State *L) {
    Plug *p; char rel[600], abs[2000]; ExPathInfo info; char *t; size_t len = 0;
    need(L, "vault.read"); p = plug_of(L);
    path_arg(L, 1, rel, sizeof rel, 0);
    abs_path(p->P, rel, abs, sizeof abs);
    if (!inside_vault(p->P, abs)) { lua_pushnil(L); lua_pushstring(L, "not found"); return 2; }     /* a link that leads out of the vault is as good as absent */
    if (ex_platform_path_info(abs, &info) != 0 || !info.is_file) { lua_pushnil(L); lua_pushstring(L, "not found"); return 2; }
    if (info.size > (int64_t)MAX_READ) { lua_pushnil(L); lua_pushstring(L, "the file is over 8 MB"); return 2; }
    t = ex_platform_read_file(abs, &len);
    if (!t) { lua_pushnil(L); lua_pushstring(L, "the file could not be read"); return 2; }
    lua_pushlstring(L, t, len);
    free(t);
    return 1;
}

static int l_vault_exists(lua_State *L) {
    Plug *p; char rel[600], abs[2000]; ExPathInfo info;
    need(L, "vault.read"); p = plug_of(L);
    path_arg(L, 1, rel, sizeof rel, 0);
    abs_path(p->P, rel, abs, sizeof abs);
    lua_pushboolean(L, inside_vault(p->P, abs) && ex_platform_path_info(abs, &info) == 0);
    return 1;
}

typedef struct ListCtx { char **v; size_t n, cap; const char *dir; int root; } ListCtx;
static SDL_EnumerationResult list_cb(void *ud, const char *dirname, const char *fname) {
    ListCtx *c = ud;
    char path[2200], *copy;
    SDL_PathInfo info;
    size_t l = strlen(fname);
    if (c->root && (!SDL_strcasecmp(fname, ".exhaust") || !SDL_strcasecmp(fname, ".trash"))) return SDL_ENUM_CONTINUE;
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    copy = malloc(l + 2);
    if (!copy) return SDL_ENUM_FAILURE;
    strcpy(copy, fname);
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) { copy[l] = '/'; copy[l + 1] = 0; }
    if (c->n == c->cap) { size_t nc = c->cap ? c->cap * 2 : 32; char **nv = realloc(c->v, nc * sizeof *nv); if (!nv) { free(copy); return SDL_ENUM_FAILURE; } c->v = nv; c->cap = nc; }
    c->v[c->n++] = copy;
    return c->n >= 5000 ? SDL_ENUM_SUCCESS : SDL_ENUM_CONTINUE;
}
static int cmp_s(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int l_vault_list(lua_State *L) {
    Plug *p; char rel[600], abs[2000]; ListCtx c; size_t i; ExPathInfo info;
    need(L, "vault.read"); p = plug_of(L);
    path_arg(L, 1, rel, sizeof rel, 1);
    abs_path(p->P, rel, abs, sizeof abs);
    if (!inside_vault(p->P, abs) || ex_platform_path_info(abs, &info) != 0 || !info.is_dir) { lua_pushnil(L); lua_pushstring(L, "not a folder"); return 2; }
    memset(&c, 0, sizeof c); c.root = rel[0] == 0;
    SDL_EnumerateDirectory(abs, list_cb, &c);
    qsort(c.v, c.n, sizeof *c.v, cmp_s);
    lua_createtable(L, (int)c.n, 0);
    for (i = 0; i < c.n; i++) { lua_pushstring(L, c.v[i]); lua_rawseti(L, -2, (lua_Integer)i + 1); free(c.v[i]); }
    free(c.v);
    return 1;
}

static int l_vault_write(lua_State *L) {
    Plug *p; char rel[600], abs[2000], dir[2000], *sl; size_t len; const char *t;
    need(L, "vault.write"); p = plug_of(L);
    path_arg(L, 1, rel, sizeof rel, 0);
    t = luaL_checklstring(L, 2, &len);
    if (len > MAX_WRITE) { lua_pushnil(L); lua_pushstring(L, "the text is over 16 MB"); return 2; }
    abs_path(p->P, rel, abs, sizeof abs);
    if (!inside_vault(p->P, abs)) { lua_pushnil(L); lua_pushstring(L, "the path leads out of the vault"); return 2; }   /* nothing is made before this */
    snprintf(dir, sizeof dir, "%s", abs);
    sl = strrchr(dir, '/'); if (sl) { *sl = 0; if (ex_platform_make_dirs(dir) != 0) { lua_pushnil(L); lua_pushstring(L, "the folder could not be made"); return 2; } }
    if (ex_platform_write_file_atomic(abs, t, len) != 0) { lua_pushnil(L); lua_pushstring(L, "the file could not be written"); return 2; }
    lua_pushboolean(L, 1);
    return 1;
}

static int l_vault_rename(lua_State *L) {
    Plug *p; char from[600], to[600]; Req *r;
    need(L, "vault.write"); p = plug_of(L);
    path_arg(L, 1, from, sizeof from, 0); path_arg(L, 2, to, sizeof to, 0);
    {
        char af[2000], at[2000], *sl;
        abs_path(p->P, from, af, sizeof af); abs_path(p->P, to, at, sizeof at);
        sl = strrchr(af, '/'); if (sl) *sl = 0;               /* the link itself may be renamed; the folders around it must be in the vault */
        sl = strrchr(at, '/'); if (sl) *sl = 0;
        if (!inside_vault(p->P, af) || !inside_vault(p->P, at)) { lua_pushnil(L); lua_pushstring(L, "the path leads out of the vault"); return 2; }
    }
    r = calloc(1, sizeof *r);
    if (!r) return luaL_error(L, "not enough memory");
    r->kind = R_RENAME; r->s1 = dup_s(from); r->s2 = dup_s(to);
    if (call_ui(p, r) != 0) { lua_pushnil(L); lua_pushstring(L, "the application did not answer"); return 2; }
    if (r->rc != 0) { free_req(r); lua_pushnil(L); lua_pushstring(L, "the file could not be renamed (it is missing or the new name is taken)"); return 2; }
    free_req(r);
    lua_pushboolean(L, 1);
    return 1;
}

static Req *editor_get(lua_State *L, int *ok) {
    Plug *p = plug_of(L);
    Req *r = calloc(1, sizeof *r);
    if (!r) { *ok = 0; luaL_error(L, "not enough memory"); }
    r->kind = R_EDITOR_GET;
    if (call_ui(p, r) != 0) { *ok = 0; luaL_error(L, "the application did not answer"); }
    *ok = r->rc == 0 && r->text != NULL;
    return r;
}

static int l_editor_text(lua_State *L) {
    int ok; Req *r;
    need(L, "editor.read");
    r = editor_get(L, &ok);
    if (ok) lua_pushlstring(L, r->text, r->len); else lua_pushnil(L);
    free_req(r);
    return 1;
}
static int l_editor_selection(lua_State *L) {
    int ok; Req *r;
    need(L, "editor.read");
    r = editor_get(L, &ok);
    if (ok && r->n0 <= r->n1 && r->n1 <= r->len) { lua_pushlstring(L, r->text + r->n0, r->n1 - r->n0); lua_pushinteger(L, (lua_Integer)r->n0); lua_pushinteger(L, (lua_Integer)r->n1); free_req(r); return 3; }
    free_req(r);
    lua_pushnil(L);
    return 1;
}
static int l_editor_cursor(lua_State *L) {
    int ok; Req *r;
    need(L, "editor.read");
    r = editor_get(L, &ok);
    if (ok) lua_pushinteger(L, (lua_Integer)r->n2); else lua_pushnil(L);
    free_req(r);
    return 1;
}
static int editor_put(lua_State *L, int whole) {
    Plug *p; Req *r; size_t len; const char *t;
    need(L, "editor.write"); p = plug_of(L);
    t = luaL_checklstring(L, 1, &len);
    if (len > MAX_WRITE) return luaL_error(L, "the text is over 16 MB");
    r = calloc(1, sizeof *r);
    if (!r) return luaL_error(L, "not enough memory");
    r->kind = R_EDITOR_REPLACE; r->i1 = whole; r->text = malloc(len + 1); r->len = len;
    if (!r->text) { free_req(r); return luaL_error(L, "not enough memory"); }
    memcpy(r->text, t, len); r->text[len] = 0;
    if (call_ui(p, r) != 0) return luaL_error(L, "the application did not answer");
    if (r->rc != 0) { free_req(r); lua_pushnil(L); lua_pushstring(L, "no note is open"); return 2; }
    free_req(r);
    lua_pushboolean(L, 1);
    return 1;
}
static int l_editor_replace_selection(lua_State *L) { return editor_put(L, 0); }
static int l_editor_set_text(lua_State *L) { return editor_put(L, 1); }

static int l_ui_notice(lua_State *L) {
    Plug *p; Req *r; const char *t;
    need(L, "ui.notice"); p = plug_of(L);
    t = luaL_checkstring(L, 1);
    r = calloc(1, sizeof *r);
    if (!r) return 0;
    { char m[400]; snprintf(m, sizeof m, "%.100s: %.280s", p->info.name, t); r->s1 = dup_s(m); }
    r->kind = R_NOTICE; r->nowait = 1;
    post_req(p->P, r);
    return 0;
}

static int id_ok(const char *id) {
    size_t i, n = strlen(id);
    if (n == 0 || n > 40) return 0;
    for (i = 0; i < n; i++) if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return 0;
    return 1;
}

static int add_command(Plug *p, const char *cid, const char *name, int editor) {
    Req *r = calloc(1, sizeof *r);
    int rc;
    if (!r) return -1;
    r->kind = R_CMD_ADD; r->s1 = dup_s(p->info.id); r->s2 = dup_s(p->info.name); r->s3 = dup_s(cid); r->s4 = dup_s(name); r->i1 = editor;
    if (call_ui(p, r) != 0) return -1;
    rc = r->rc;
    free_req(r);
    return rc;
}

static int l_commands_register(lua_State *L) {
    Plug *p; const char *id, *name; int editor = 0; CmdRef *c;
    need(L, "commands.register"); p = plug_of(L);
    id = luaL_checkstring(L, 1); name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (lua_istable(L, 4)) { lua_getfield(L, 4, "editor"); editor = lua_toboolean(L, -1); lua_pop(L, 1); }
    if (!id_ok(id)) return luaL_error(L, "a command id has lowercase letters, digits and hyphens only");
    if (strlen(name) > 100 || !*name) return luaL_error(L, "a command name is 1 to 100 bytes");
    if (p->ncmds >= MAX_CMDS) return luaL_error(L, "too many commands");
    { size_t i; for (i = 0; i < p->ncmds; i++) if (!strcmp(p->cmds[i].id, id)) return luaL_error(L, "the command %s is already registered", id); }
    c = &p->cmds[p->ncmds];
    snprintf(c->id, sizeof c->id, "%s", id); c->editor = editor;
    lua_pushvalue(L, 3);
    c->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    p->ncmds++;
    if (add_command(p, id, name, editor) != 0) { p->ncmds--; luaL_unref(L, LUA_REGISTRYINDEX, c->ref); return luaL_error(L, "the command could not be added"); }
    return 0;
}

static int l_commands_run(lua_State *L) {
    Plug *p; Req *r; const char *id;
    need(L, "commands.register"); p = plug_of(L);
    id = luaL_checkstring(L, 1);
    r = calloc(1, sizeof *r);
    if (!r) return luaL_error(L, "not enough memory");
    {
        char full[200], own[100];
        if (strchr(id, '.')) snprintf(full, sizeof full, "%s", id); else snprintf(full, sizeof full, "plugin.%s.%s", p->info.id, id);
        snprintf(own, sizeof own, "plugin.%s.", p->info.id);
        if (strncmp(full, own, strlen(own)) != 0 && !has_perm(p, "vault.write") && !has_perm(p, "editor.write")) {   /* an application command may delete, rename or rewrite notes */
            free(r);
            return luaL_error(L, "permission denied: vault.write or editor.write (to run a command of the application)");
        }
        r->s1 = dup_s(full);
    }
    r->kind = R_CMD_RUN; r->nowait = 1;
    post_req(p->P, r);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_add_pane(lua_State *L) {
    Plug *p; const char *id, *title; PaneRef *pr; char cid[80], name[160];
    need(L, "ui.pane"); p = plug_of(L);
    id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!id_ok(id)) return luaL_error(L, "a pane id has lowercase letters, digits and hyphens only");
    if (p->npanes >= MAX_PANES) return luaL_error(L, "too many panes");
    lua_getfield(L, 2, "title"); title = lua_tostring(L, -1);
    if (!title || !*title || strlen(title) > 100) return luaL_error(L, "a pane needs a title of 1 to 100 bytes");
    pr = &p->panes[p->npanes];
    memset(pr, 0, sizeof *pr);
    snprintf(pr->id, sizeof pr->id, "%s", id); snprintf(pr->title, sizeof pr->title, "%s", title);
    pr->render_ref = LUA_NOREF;
    lua_pop(L, 1);
    lua_getfield(L, 2, "text");
    if (lua_isstring(L, -1)) { size_t l; const char *t = lua_tolstring(L, -1, &l); pr->text = malloc(l + 1); if (pr->text) { memcpy(pr->text, t, l); pr->text[l] = 0; } }
    lua_pop(L, 1);
    lua_getfield(L, 2, "render");
    if (lua_isfunction(L, -1)) pr->render_ref = luaL_ref(L, LUA_REGISTRYINDEX); else lua_pop(L, 1);
    if (!pr->text && pr->render_ref == LUA_NOREF) return luaL_error(L, "a pane needs text or a render function");
    p->npanes++;
    snprintf(cid, sizeof cid, "pane-%s", id); snprintf(name, sizeof name, "Open %.100s", pr->title);
    if (add_command(p, cid, name, 0) != 0) return luaL_error(L, "the pane could not be added");
    return 0;
}

/* a plugin's own settings: .exhaust/plugins/<id>/data.ini */
/* The value of a plugin setting is kept in INI, which trims it and cuts it at " #", so the bytes it cannot hold are written as %XX (docs/formats.md, Settings of a plugin). */
static char *enc_value(const char *v, size_t n, size_t *outlen) {
    char *o = malloc(n * 3 + 1);
    size_t i, w = 0;
    if (!o) return NULL;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)v[i];
        if (c == '%' || c == '#' || c < 0x20 || c == 0x7F || (c == ' ' && (i == 0 || i == n - 1))) { snprintf(o + w, 4, "%%%02X", c); w += 3; }
        else o[w++] = (char)c;
    }
    o[w] = 0;
    *outlen = w;
    return o;
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static size_t dec_value(const char *v, char *out) {
    size_t i = 0, w = 0, n = strlen(v);
    while (i < n) {
        if (v[i] == '%' && i + 2 < n && hexv((unsigned char)v[i + 1]) >= 0 && hexv((unsigned char)v[i + 2]) >= 0) { out[w++] = (char)(hexv((unsigned char)v[i + 1]) * 16 + hexv((unsigned char)v[i + 2])); i += 3; }
        else out[w++] = v[i++];
    }
    out[w] = 0;
    return w;
}

static int l_settings_get(lua_State *L) {
    Plug *p; char path[1500]; char *t; size_t len = 0; ExIni *ini; const char *v;
    need(L, "settings.own"); p = plug_of(L);
    { const char *k = luaL_checkstring(L, 1); snprintf(path, sizeof path, "%s/data.ini", p->folder);
      t = ex_platform_read_file(path, &len);
      if (!t) { lua_pushnil(L); return 1; }
      ini = ex_ini_parse(t, len); free(t);
      v = ini ? ex_ini_get(ini, "data", k) : NULL;
      if (v) { char *d = malloc(strlen(v) + 1); if (!d) { ex_ini_free(ini); return luaL_error(L, "not enough memory"); } lua_pushlstring(L, d, dec_value(v, d)); free(d); } else lua_pushnil(L);
      ex_ini_free(ini); }
    return 1;
}
static int l_settings_set(lua_State *L) {
    Plug *p; char path[1500]; char *t, *out, *ev; size_t len = 0, ol = 0, vlen = 0, evlen = 0; const char *k, *v; ExIni *ini; size_t i;
    need(L, "settings.own"); p = plug_of(L);
    k = luaL_checkstring(L, 1); v = luaL_checklstring(L, 2, &vlen);
    if (strlen(k) > 1000 || vlen > 1000 || !*k || strpbrk(k, "=[]\r\n")) return luaL_error(L, "a setting has a key without = [ ] or line breaks, up to 1,000 bytes for the key and for the value");
    ev = enc_value(v, vlen, &evlen);
    if (!ev) return luaL_error(L, "not enough memory");
    snprintf(path, sizeof path, "%s/data.ini", p->folder);
    t = ex_platform_read_file(path, &len);
    if (!t) { t = dup_s(""); len = 0; }
    ini = ex_ini_parse(t, len);
    if (ini && !ex_ini_get(ini, "data", k) && ex_ini_key_count(ini, "data") >= 200) { ex_ini_free(ini); free(t); free(ev); return luaL_error(L, "too many settings (200)"); }
    ex_ini_free(ini);
    out = ex_ini_patch(t, len, "data", k, ev, &ol);
    free(t); free(ev); (void)evlen;
    (void)i;
    if (!out || ex_platform_write_file_atomic(path, out, ol) != 0) { free(out); lua_pushnil(L); lua_pushstring(L, "the settings could not be saved"); return 2; }
    free(out);
    lua_pushboolean(L, 1);
    return 1;
}

static const char *const EVENTS[] = { "vault-open", "file-open", "file-save", "file-rename", "editor-change" };
static int l_events_on(lua_State *L) {
    Plug *p = plug_of(L); const char *name; size_t i; int ok = 0;
    name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) if (!strcmp(EVENTS[i], name)) ok = 1;
    if (!ok) return luaL_error(L, "unknown event: %s", name);
    if (p->nevs >= MAX_EVS) return luaL_error(L, "too many event handlers");
    snprintf(p->evs[p->nevs].name, sizeof p->evs[0].name, "%s", name);
    lua_pushvalue(L, 2);
    p->evs[p->nevs].ref = luaL_ref(L, LUA_REGISTRYINDEX);
    p->nevs++;
    return 0;
}

static int l_http_get(lua_State *L) {
    need(L, "network");
    (void)luaL_checkstring(L, 1);
    return luaL_error(L, "network use is off");                  /* nothing in 0.1.0 makes a request */
}

static void setfn(lua_State *L, const char *name, lua_CFunction f) { lua_pushcfunction(L, f); lua_setfield(L, -2, name); }

static void make_api(lua_State *L) {
    lua_newtable(L);
    setfn(L, "version", l_version);
    lua_newtable(L); setfn(L, "register", l_commands_register); setfn(L, "run", l_commands_run); lua_setfield(L, -2, "commands");
    lua_newtable(L); setfn(L, "read", l_vault_read); setfn(L, "list", l_vault_list); setfn(L, "exists", l_vault_exists); setfn(L, "write", l_vault_write); setfn(L, "rename", l_vault_rename); lua_setfield(L, -2, "vault");
    lua_newtable(L); setfn(L, "text", l_editor_text); setfn(L, "selection", l_editor_selection); setfn(L, "cursor", l_editor_cursor);
    setfn(L, "replace_selection", l_editor_replace_selection); setfn(L, "set_text", l_editor_set_text); lua_setfield(L, -2, "editor");
    lua_newtable(L); setfn(L, "notice", l_ui_notice); setfn(L, "add_pane", l_add_pane); lua_setfield(L, -2, "ui");
    lua_newtable(L); setfn(L, "get", l_settings_get); setfn(L, "set", l_settings_set); lua_setfield(L, -2, "settings");
    lua_newtable(L); setfn(L, "on", l_events_on); lua_setfield(L, -2, "events");
    lua_newtable(L); setfn(L, "get", l_http_get); lua_setfield(L, -2, "http");
    lua_setglobal(L, "exhaust");
}

static void open_sandbox(lua_State *L) {
    static const char *const gone[] = { "dofile", "loadfile", "load", "loadstring", "require", "collectgarbage" };
    size_t i;
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
    for (i = 0; i < sizeof gone / sizeof *gone; i++) { lua_pushnil(L); lua_setglobal(L, gone[i]); }
    lua_pushcfunction(L, l_pcall); lua_setglobal(L, "pcall");
    lua_pushcfunction(L, l_xpcall); lua_setglobal(L, "xpcall");
    lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
    lua_getglobal(L, "coroutine"); lua_getfield(L, -1, "resume"); lua_pushcclosure(L, l_resume, 1); lua_setfield(L, -2, "resume"); lua_pop(L, 1);
    lua_getglobal(L, "string"); lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);          /* no bytecode */
    make_api(L);
}

/* ---- the plugin thread ---- */

static void changed(ExPlugins *P) {
    ExMessage m;
    memset(&m, 0, sizeof m);
    m.type = MSG_CHANGED;
    if (ex_queue_push(P->queue, &m) == 0 && P->wake) P->wake(P->wake_user);
}

static void remove_commands(Plug *p) {
    Req *r = calloc(1, sizeof *r);
    char prefix[80];
    if (!r) return;
    snprintf(prefix, sizeof prefix, "plugin.%s.", p->info.id);
    r->kind = R_CMDS_REMOVE; r->s1 = dup_s(prefix); r->nowait = 1;
    post_req(p->P, r);
}

static void free_plug(Plug *p) {
    size_t i;
    if (p->L) lua_close(p->L);
    for (i = 0; i < p->npanes; i++) free(p->panes[i].text);
    free(p);
}

static void unload(ExPlugins *P, size_t idx) {
    Plug *p = P->plugs[idx];
    remove_commands(p);
    free_plug(p);
    memmove(&P->plugs[idx], &P->plugs[idx + 1], (P->nplugs - idx - 1) * sizeof *P->plugs);
    P->nplugs--;
}

static void mark_stopped(ExPlugins *P, const char *id, const char *err) {
    size_t i;
    for (i = 0; i < P->nstopped; i++) if (!strcmp(P->stopped[i].id, id)) { snprintf(P->stopped[i].error, sizeof P->stopped[i].error, "%s", err); return; }
    if (P->nstopped < MAX_PLUGINS) { snprintf(P->stopped[P->nstopped].id, sizeof P->stopped[0].id, "%s", id); snprintf(P->stopped[P->nstopped].error, sizeof P->stopped[0].error, "%s", err); P->nstopped++; }
}
static void clear_stopped(ExPlugins *P, const char *id) {
    size_t i;
    for (i = 0; i < P->nstopped; i++) if (!strcmp(P->stopped[i].id, id)) { memmove(&P->stopped[i], &P->stopped[i + 1], (P->nstopped - i - 1) * sizeof *P->stopped); P->nstopped--; return; }
}

/* runs what is on the stack (function and nargs arguments) under a time limit; 0 ok, -1 an error that was reported, -2 a limit: the plugin is stopped */
static int run_limited(ExPlugins *P, Plug *p, int nargs, uint64_t limit_ms, const char *what) {
    int st;
    char msg[300];
    p->start_ms = ex_platform_now_ms(); p->waited_ms = 0; p->limit_ms = limit_ms; p->over = 0; p->active = 1;
    st = lua_pcall(p->L, nargs, 0, 0);
    p->active = 0;
    if (st == LUA_OK) return 0;
    snprintf(msg, sizeof msg, "%s", lua_tostring(p->L, -1) ? lua_tostring(p->L, -1) : "error");
    lua_pop(p->L, 1);
    if (p->over || p->mem.exceeded) {
        snprintf(msg, sizeof msg, "%s", p->over ? "time limit exceeded" : "not enough memory");
        log_line(p, msg);
        mark_stopped(P, p->info.id, msg);
        send_notice(p, "%s was stopped: %s", msg);
        return -2;
    }
    { char line[400]; snprintf(line, sizeof line, "error in %s: %s", what, msg); log_line(p, line); }
    send_notice(p, "%s: %s", msg);
    return -1;
}

static void load_plugin(ExPlugins *P, const ExPluginInfo *info, const char *folder) {
    Plug *p;
    char path[1600], *text, chunk[160];
    size_t len = 0;
    if (P->nplugs >= MAX_PLUGINS) return;
    p = calloc(1, sizeof *p);
    if (!p) return;
    p->info = *info; p->P = P;
    snprintf(p->folder, sizeof p->folder, "%s", folder);
    p->mem.limit = MEM_LIMIT;
    p->L = lua_newstate(l_alloc, &p->mem);
    if (!p->L) { free(p); return; }
    *(Plug **)lua_getextraspace(p->L) = p;
    lua_sethook(p->L, hook, LUA_MASKCOUNT, 1000);
    open_sandbox(p->L);
    snprintf(path, sizeof path, "%s/%s", folder, info->entry);
    text = ex_platform_read_file(path, &len);
    if (!text || len > 1024 * 1024) {
        free(text);
        mark_stopped(P, info->id, text ? "the entry file is over 1 MB" : "the entry file could not be read");
        free_plug(p);
        return;
    }
    snprintf(chunk, sizeof chunk, "@%s/%s", info->id, info->entry);
    if (luaL_loadbufferx(p->L, text, len, chunk, "t") != LUA_OK) {
        char m[240]; snprintf(m, sizeof m, "%s", lua_tostring(p->L, -1) ? lua_tostring(p->L, -1) : "syntax error");
        free(text);
        log_line(p, m);
        mark_stopped(P, info->id, m);
        free_plug(p);
        return;
    }
    free(text);
    P->plugs[P->nplugs++] = p;
    if (run_limited(P, p, 0, LOAD_LIMIT_MS, "loading") != 0) {
        size_t i;
        for (i = 0; i < P->nplugs; i++) if (P->plugs[i] == p) { char err[200]; snprintf(err, sizeof err, "%s", "the entry file failed (see log.txt)"); mark_stopped(P, info->id, p->over ? "time limit exceeded" : p->mem.exceeded ? "not enough memory" : err); unload(P, i); break; }
    }
}

/* ---- the approvals: .exhaust/plugins.ini ---- */

static int set_equal(const char *list, const ExPluginInfo *info) {
    char l[400], tok[40];
    const char *q;
    size_t found = 0, count = 0, k;
    snprintf(l, sizeof l, "%s", list ? list : "");
    q = l;
    while (*q) {
        size_t tl = 0;
        int in = 0;
        while (*q == ',' || *q == ' ') q++;
        while (*q && *q != ',' && *q != ' ' && tl < sizeof tok - 1) tok[tl++] = *q++;
        tok[tl] = 0;
        if (!tl) break;
        count++;
        for (k = 0; k < info->nperm; k++) if (!strcmp(info->permissions[k], tok)) in = 1;
        if (in) found++;
    }
    return count == info->nperm && found == info->nperm;
}

static void sec_name(char *out, size_t cap, const char *id) { snprintf(out, cap, "plugin.%s", id); }

static void apply_state(ExPlugins *P, ExIni *ini, ExPluginInfo *info) {
    char sec[100];
    const char *ver, *appr;
    size_t i;
    sec_name(sec, sizeof sec, info->id);
    info->enabled = ex_ini_get_bool(ini, sec, "enabled", 0);
    ver = ex_ini_get(ini, sec, "version"); appr = ex_ini_get(ini, sec, "approved");
    info->approved = info->valid && info->enabled && ver && !strcmp(ver, info->version) && set_equal(appr, info);
    info->running = 0;
    for (i = 0; i < P->nplugs; i++) if (!strcmp(P->plugs[i]->info.id, info->id)) info->running = 1;
    if (!info->valid) snprintf(info->status, sizeof info->status, "Invalid: %s", info->error);
    else if (info->running) snprintf(info->status, sizeof info->status, "Running");
    else if (!info->enabled) snprintf(info->status, sizeof info->status, "Off");
    else if (!info->approved) snprintf(info->status, sizeof info->status, "Needs approval again: the permissions or the version changed");
    else if (!SDL_GetAtomicInt(&P->allowed)) snprintf(info->status, sizeof info->status, "On, waiting: plugins are not allowed (turn on Allow plugins)");
    else {
        const char *err = NULL;
        for (i = 0; i < P->nstopped; i++) if (!strcmp(P->stopped[i].id, info->id)) err = P->stopped[i].error;
        if (err) snprintf(info->status, sizeof info->status, "Stopped: %s", err); else snprintf(info->status, sizeof info->status, "On");
    }
}

typedef struct DirCtx { char names[MAX_PLUGINS][64]; size_t n; } DirCtx;
static SDL_EnumerationResult dir_cb(void *ud, const char *dirname, const char *fname) {
    DirCtx *c = ud;
    char path[1600];
    SDL_PathInfo pi;
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    if (fname[0] != '.' && strlen(fname) < 64 && c->n < MAX_PLUGINS && SDL_GetPathInfo(path, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY) snprintf(c->names[c->n++], 64, "%s", fname);
    return SDL_ENUM_CONTINUE;
}
static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void do_scan(ExPlugins *P) {
    DirCtx dc;
    ExIni *ini;
    char *t;
    size_t len = 0, i, n = 0;
    ExPluginInfo *list = calloc(MAX_PLUGINS, sizeof *list);
    if (!list) return;
    memset(&dc, 0, sizeof dc);
    SDL_EnumerateDirectory(P->dir, dir_cb, &dc);
    qsort(dc.names, dc.n, sizeof dc.names[0], cmp_names);
    t = ex_platform_read_file(P->ini_path, &len);
    ini = ex_ini_parse(t ? t : "", t ? len : 0);
    free(t);
    for (i = 0; i < dc.n && n < MAX_PLUGINS; i++) {
        char folder[1500];
        snprintf(folder, sizeof folder, "%s/%s", P->dir, dc.names[i]);
        ex_plugin_read_manifest(folder, dc.names[i], &list[n]);
        n++;
    }
    /* stop what must not run: not on, not approved, changed, or all of them when plugins are not allowed */
    for (i = P->nplugs; i-- > 0;) {
        size_t k;
        int keep = 0;
        for (k = 0; k < n; k++) {
            if (strcmp(list[k].id, P->plugs[i]->info.id)) continue;
            apply_state(P, ini, &list[k]);
            if (list[k].valid && list[k].approved && SDL_GetAtomicInt(&P->allowed) && !strcmp(list[k].version, P->plugs[i]->info.version) && list[k].nperm == P->plugs[i]->info.nperm) keep = 1;
        }
        if (!keep) unload(P, i);
    }
    /* start what must run */
    for (i = 0; i < n; i++) {
        size_t k;
        int running = 0;
        char folder[1500], err[200];
        apply_state(P, ini, &list[i]);
        if (!list[i].valid || !list[i].approved || !SDL_GetAtomicInt(&P->allowed)) continue;
        for (k = 0; k < P->nplugs; k++) if (!strcmp(P->plugs[k]->info.id, list[i].id)) running = 1;
        for (k = 0; k < P->nstopped; k++) if (!strcmp(P->stopped[k].id, list[i].id)) running = 1;       /* stopped for a reason: stays off until the user turns it on again */
        if (running) continue;
        snprintf(folder, sizeof folder, "%s/%s", P->dir, list[i].id);
        load_plugin(P, &list[i], folder);
        (void)err;
    }
    for (i = 0; i < n; i++) apply_state(P, ini, &list[i]);
    ex_ini_free(ini);
    SDL_LockMutex(P->mu);
    memcpy(P->infos, list, n * sizeof *list);
    P->ninfos = n;
    SDL_UnlockMutex(P->mu);
    free(list);
    changed(P);
}

static void do_enable(ExPlugins *P, const char *id, int on) {
    char folder[1500], sec[100], perms[400] = "", *t, *out;
    ExPluginInfo info;
    size_t len = 0, ol = 0, i;
    snprintf(folder, sizeof folder, "%s/%s", P->dir, id);
    ex_plugin_read_manifest(folder, id, &info);
    sec_name(sec, sizeof sec, id);
    t = ex_platform_read_file(P->ini_path, &len);
    if (!t) { t = dup_s(""); len = 0; }
    for (i = 0; i < info.nperm; i++) { size_t l = strlen(perms); snprintf(perms + l, sizeof perms - l, "%s%s", i ? "," : "", info.permissions[i]); }
    out = ex_ini_patch(t, len, sec, "enabled", on ? "true" : "false", &ol);
    free(t);
    if (out && on && info.valid) {
        char *o2 = ex_ini_patch(out, ol, sec, "approved", perms, &len);
        free(out); out = o2;
        if (out) { o2 = ex_ini_patch(out, len, sec, "version", info.version, &ol); free(out); out = o2; }
    }
    if (out) { char dir[1500]; snprintf(dir, sizeof dir, "%s", P->ini_path); { char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; ex_platform_make_dirs(dir); } } ex_platform_write_file_atomic(P->ini_path, out, ol); free(out); }
    if (on) clear_stopped(P, id);
    else {
        for (i = 0; i < P->nplugs; i++) if (!strcmp(P->plugs[i]->info.id, id)) { unload(P, i); break; }
        clear_stopped(P, id);
    }
    do_scan(P);
}

static Plug *find_plug(ExPlugins *P, const char *id) { size_t i; for (i = 0; i < P->nplugs; i++) if (!strcmp(P->plugs[i]->info.id, id)) return P->plugs[i]; return NULL; }

static void do_cmd(ExPlugins *P, const char *pid, const char *cid) {
    Plug *p = find_plug(P, pid);
    size_t i;
    if (!p) return;
    if (!strncmp(cid, "pane-", 5)) {
        for (i = 0; i < p->npanes; i++) {
            if (strcmp(p->panes[i].id, cid + 5)) continue;
            {
                Req *r = calloc(1, sizeof *r);
                char *text = NULL;
                if (!r) return;
                if (p->panes[i].render_ref != LUA_NOREF) {
                    lua_rawgeti(p->L, LUA_REGISTRYINDEX, p->panes[i].render_ref);
                    p->start_ms = ex_platform_now_ms(); p->waited_ms = 0; p->limit_ms = CALL_LIMIT_MS; p->over = 0; p->active = 1;
                    if (lua_pcall(p->L, 0, 1, 0) == LUA_OK) { size_t l; const char *s = lua_tolstring(p->L, -1, &l); if (s) { text = malloc(l + 1); if (text) { memcpy(text, s, l); text[l] = 0; } } lua_pop(p->L, 1); }
                    else {
                        char m[300]; snprintf(m, sizeof m, "%s", lua_tostring(p->L, -1) ? lua_tostring(p->L, -1) : "error"); lua_pop(p->L, 1);
                        p->active = 0;
                        if (p->over || p->mem.exceeded) { mark_stopped(P, pid, p->over ? "time limit exceeded" : "not enough memory"); send_notice(p, "%s was stopped: %s", p->over ? "time limit exceeded" : "not enough memory"); { size_t k; for (k = 0; k < P->nplugs; k++) if (P->plugs[k] == p) { unload(P, k); break; } } do_scan(P); free(r); return; }
                        log_line(p, m); send_notice(p, "%s: %s", m);
                    }
                    p->active = 0;
                } else text = dup_s(p->panes[i].text);
                if (!text) { free(r); return; }
                r->kind = R_OPEN_PANE; r->s1 = dup_s(p->panes[i].title); r->text = text; r->len = strlen(text); r->nowait = 1;
                post_req(P, r);
            }
            return;
        }
        return;
    }
    for (i = 0; i < p->ncmds; i++) {
        int st;
        if (strcmp(p->cmds[i].id, cid)) continue;
        lua_rawgeti(p->L, LUA_REGISTRYINDEX, p->cmds[i].ref);
        st = run_limited(P, p, 0, CALL_LIMIT_MS, cid);
        if (st == -2) { size_t k; for (k = 0; k < P->nplugs; k++) if (P->plugs[k] == p) { unload(P, k); break; } do_scan(P); }
        return;
    }
}

static void do_event(ExPlugins *P, const char *name, const char *a, const char *b) {
    size_t i = 0;
    while (i < P->nplugs) {
        Plug *p = P->plugs[i];
        size_t k;
        int stopped = 0;
        for (k = 0; k < p->nevs && !stopped; k++) {
            int n = 0, st;
            if (strcmp(p->evs[k].name, name)) continue;
            lua_rawgeti(p->L, LUA_REGISTRYINDEX, p->evs[k].ref);
            if (a) { lua_pushstring(p->L, a); n++; if (b) { lua_pushstring(p->L, b); n++; } }
            st = run_limited(P, p, n, CALL_LIMIT_MS, name);
            if (st == -2) { unload(P, i); stopped = 1; }                       /* a limit: the plugin is gone, the next one is now at i */
        }
        if (stopped) do_scan(P); else i++;
    }
}

static void free_job(Job *j) { free(j->a); free(j->b); free(j->c); free(j); }

static int thread_main(void *ud) {
    ExPlugins *P = ud;
    for (;;) {
        Job *j;
        SDL_LockMutex(P->mu);
        while (!P->jobs && !P->stop) SDL_WaitCondition(P->cv, P->mu);
        if (P->stop && !P->jobs) { SDL_UnlockMutex(P->mu); break; }
        j = P->jobs; P->jobs = j->next; if (!P->jobs) P->jobs_tail = NULL;
        P->njobs--; P->running_job = 1;
        SDL_UnlockMutex(P->mu);
        switch (j->kind) {
        case J_SCAN: do_scan(P); break;
        case J_ENABLE: do_enable(P, j->a, j->i1); break;
        case J_CMD: do_cmd(P, j->a, j->b); break;
        case J_EVENT: do_event(P, j->a, j->b, j->c); break;
        case J_STOP: P->stop = 1; break;
        }
        free_job(j);
        SDL_LockMutex(P->mu); P->running_job = 0; SDL_UnlockMutex(P->mu);
    }
    {   size_t i;
        for (i = 0; i < P->nplugs; i++) free_plug(P->plugs[i]);
        P->nplugs = 0; }
    SDL_SetAtomicInt(&P->done, 1);
    return 0;
}

static void post_job(ExPlugins *P, int kind, const char *a, const char *b, const char *c, int i1) {
    Job *j = calloc(1, sizeof *j);
    if (!j) return;
    j->kind = kind; j->a = a ? dup_s(a) : NULL; j->b = b ? dup_s(b) : NULL; j->c = c ? dup_s(c) : NULL; j->i1 = i1;
    SDL_LockMutex(P->mu);
    if (kind == J_EVENT && P->njobs >= MAX_EVENTS_QUEUED) { SDL_UnlockMutex(P->mu); free_job(j); return; }
    if (P->jobs_tail) P->jobs_tail->next = j; else P->jobs = j;
    P->jobs_tail = j; P->njobs++;
    SDL_SignalCondition(P->cv);
    SDL_UnlockMutex(P->mu);
}

/* ---- the interface thread's side ---- */

typedef struct CmdUser { ExPlugins *P; char pid[64], cid[64]; } CmdUser;
static void cmd_trampoline(void *u) { CmdUser *c = u; post_job(c->P, J_CMD, c->pid, c->cid, NULL, 0); }

static void process_req(ExPlugins *P, Req *r) {
    switch (r->kind) {
    case R_EDITOR_GET: {
        char *text = NULL; size_t a = 0, b = 0, c = 0;
        r->rc = P->host.editor_get ? P->host.editor_get(P->host.user, &text, &a, &b, &c) : -1;
        if (r->rc == 0 && text) { r->text = text; r->len = strlen(text); r->n0 = a; r->n1 = b; r->n2 = c; } else free(text);
        break;
    }
    case R_EDITOR_REPLACE: r->rc = P->host.editor_replace ? P->host.editor_replace(P->host.user, r->text ? r->text : "", r->len, r->i1) : -1; break;
    case R_NOTICE: if (P->host.notice) P->host.notice(P->host.user, r->s1 ? r->s1 : ""); r->rc = 0; break;
    case R_OPEN_PANE: r->rc = P->host.open_pane ? P->host.open_pane(P->host.user, r->s1 ? r->s1 : "", r->text ? r->text : "") : -1; break;
    case R_RENAME: r->rc = P->host.rename ? P->host.rename(P->host.user, r->s1, r->s2) : -1; break;
    case R_CMD_ADD: {
        char id[260], name[300];
        CmdUser *cu;
        snprintf(id, sizeof id, "plugin.%s.%s", r->s1, r->s3);
        snprintf(name, sizeof name, "%.119s: %.150s", r->s2, r->s4);
        r->rc = -1;
        if (!P->commands) { r->rc = 0; break; }
        cu = calloc(1, sizeof *cu);
        if (!cu) break;
        cu->P = P; snprintf(cu->pid, sizeof cu->pid, "%s", r->s1); snprintf(cu->cid, sizeof cu->cid, "%s", r->s3);
        if (ex_commands_add_dynamic(P->commands, id, name, cmd_trampoline, NULL, cu, r->i1) == 0) {
            if (P->ncmd_users == P->capcmd_users) { size_t nc = P->capcmd_users ? P->capcmd_users * 2 : 16; char **nv = realloc(P->cmd_users, nc * sizeof *nv); if (nv) { P->cmd_users = nv; P->capcmd_users = nc; } }
            if (P->ncmd_users < P->capcmd_users) P->cmd_users[P->ncmd_users++] = (char *)cu; else cu = NULL;
            r->rc = 0;
        } else free(cu);
        break;
    }
    case R_CMD_RUN: if (P->commands) ex_commands_run(P->commands, r->s1); r->rc = 0; break;
    case R_CMDS_REMOVE: if (P->commands) ex_commands_remove_dynamic(P->commands, r->s1); r->rc = 0; break;
    }
}

void ex_plugins_update(ExPlugins *P) {
    ExMessage m;
    int did_change = 0;
    uint64_t t0 = ex_platform_now_ms();
    if (!P) return;
    while (ex_queue_pop(P->queue, &m)) {
        if (m.type == MSG_CHANGED) did_change = 1;
        else if (m.type == MSG_REQ && m.payload) {
            Req *r = m.payload;
            process_req(P, r);
            if (r->nowait) free_req(r);
            else if (SDL_CompareAndSwapAtomicInt(&r->state, 0, 1)) SDL_SignalSemaphore(r->sem);
            else free_req(r);                                                   /* the plugin gave up waiting */
        }
        if (ex_platform_now_ms() - t0 > 4) break;                              /* a frame's share: the rest waits for the next one */
    }
    if (did_change && P->host.changed) P->host.changed(P->host.user);
}

ExPlugins *ex_plugins_create(const char *vault_root, ExCommands *commands, const ExPluginHost *host, void (*wake)(void *), void *wake_user) {
    ExPlugins *P;
    if (!vault_root) return NULL;
    P = calloc(1, sizeof *P);
    if (!P) return NULL;
    snprintf(P->root, sizeof P->root, "%s", vault_root);
    { size_t n = strlen(P->root); while (n > 1 && P->root[n - 1] == '/') P->root[--n] = 0; }
    snprintf(P->dir, sizeof P->dir, "%s/.exhaust/plugins", P->root);
    snprintf(P->ini_path, sizeof P->ini_path, "%s/.exhaust/plugins.ini", P->root);
    P->commands = commands;
    if (host) P->host = *host;
    P->wake = wake; P->wake_user = wake_user;
    P->queue = ex_queue_create(wake, wake_user);
    P->mu = SDL_CreateMutex(); P->cv = SDL_CreateCondition();
    if (!P->queue || !P->mu || !P->cv) { ex_plugins_free(P); return NULL; }
    P->thread = SDL_CreateThread(thread_main, "plugins", P);
    if (!P->thread) { ex_plugins_free(P); return NULL; }
    return P;
}

void ex_plugins_free(ExPlugins *P) {
    Job *j;
    size_t i;
    if (!P) return;
    if (P->thread) {
        int waited = 0;
        SDL_LockMutex(P->mu); P->stop = 1; SDL_SignalCondition(P->cv); SDL_UnlockMutex(P->mu);
        while (!SDL_GetAtomicInt(&P->done) && waited < 2000) { SDL_Delay(5); waited += 5; }
        if (SDL_GetAtomicInt(&P->done)) SDL_WaitThread(P->thread, NULL);
        else { SDL_DetachThread(P->thread); return; }           /* stuck in C code: the process exit will end it; its memory is not freed */
    }
    for (j = P->jobs; j;) { Job *n = j->next; free_job(j); j = n; }
    if (P->queue) {                                              /* requests nobody answered */
        ExMessage m;
        while (ex_queue_pop(P->queue, &m)) if (m.type == MSG_REQ && m.payload) free_req(m.payload);
        ex_queue_destroy(P->queue);
    }
    for (i = 0; i < P->ncmd_users; i++) free(P->cmd_users[i]);
    free(P->cmd_users);
    if (P->commands) ex_commands_remove_dynamic(P->commands, "plugin.");
    if (P->cv) SDL_DestroyCondition(P->cv);
    if (P->mu) SDL_DestroyMutex(P->mu);
    free(P);
}

void ex_plugins_set_allowed(ExPlugins *P, int allowed) { if (!P) return; SDL_SetAtomicInt(&P->allowed, allowed != 0); post_job(P, J_SCAN, NULL, NULL, NULL, 0); }
void ex_plugins_rescan(ExPlugins *P) { if (P) post_job(P, J_SCAN, NULL, NULL, NULL, 0); }
void ex_plugins_enable(ExPlugins *P, const char *id, int on) { if (P && id) post_job(P, J_ENABLE, id, NULL, NULL, on != 0); }
void ex_plugins_emit(ExPlugins *P, const char *name, const char *a, const char *b) {
    if (!P || !name) return;
    if (!strcmp(name, "editor-change")) {
        uint64_t now = ex_platform_now_ms();
        if (now - P->last_editor_event < 500) return;
        P->last_editor_event = now;
    }
    post_job(P, J_EVENT, name, a, b, 0);
}
int ex_plugins_busy(ExPlugins *P) { int b; if (!P) return 0; SDL_LockMutex(P->mu); b = P->njobs > 0 || P->running_job; SDL_UnlockMutex(P->mu); return b; }

size_t ex_plugins_snapshot(ExPlugins *P, ExPluginInfo *out, size_t max) {
    size_t n, i;
    if (!P) return 0;
    SDL_LockMutex(P->mu);
    n = P->ninfos;
    for (i = 0; out && i < n && i < max; i++) out[i] = P->infos[i];
    SDL_UnlockMutex(P->mu);
    return n;
}
