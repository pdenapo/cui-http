/* backend.c - pure C99 backend for cui-http.
 *
 * Replaces the former D backend. Provides SQLite persistence, an HTTP engine
 * (libcurl), an async worker thread (pthreads) and the same ABI the C
 * frontend expects (see backend.h).
 *
 * Memory contract: every char* handed to the caller is malloc'ed and must be
 * released with free_response() / free_history_row() / free (for get_setting
 * and default_db_path results, use free()). */

#define _POSIX_C_SOURCE 200809L
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
typedef CRITICAL_SECTION mutex_t;
typedef CONDITION_VARIABLE cond_t;
typedef HANDLE thread_t;
#define MUTEX_INIT(m) InitializeCriticalSection(&(m))
#define MUTEX_LOCK(m) EnterCriticalSection(&(m))
#define MUTEX_UNLOCK(m) LeaveCriticalSection(&(m))
#define COND_INIT(c) InitializeConditionVariable(&(c))
#define COND_WAIT(c,m) SleepConditionVariableCS(&(c),&(m),INFINITE)
#define COND_SIGNAL(c) WakeConditionVariable(&(c))
#define COND_BROADCAST(c) WakeAllConditionVariable(&(c))
static DWORD WINAPI win_worker(LPVOID arg);
#define THREAD_CREATE(t,fn,arg) ((t)=CreateThread(NULL,0,win_worker,(arg),0,NULL))!=NULL
#define THREAD_JOIN(t) (WaitForSingleObject((t),INFINITE),CloseHandle((t)))
#define MKDIR(d) _mkdir(d)
#else
#include <pthread.h>
#include <sys/stat.h>
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t  cond_t;
typedef pthread_t       thread_t;
#define MUTEX_INIT(m) pthread_mutex_init(&(m),NULL)
#define MUTEX_LOCK(m) pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))
#define COND_INIT(c) pthread_cond_init(&(c),NULL)
#define COND_WAIT(c,m) pthread_cond_wait(&(c),&(m))
#define COND_SIGNAL(c) pthread_cond_signal(&(c))
#define COND_BROADCAST(c) pthread_cond_broadcast(&(c))
#define THREAD_CREATE(t,fn,arg) (pthread_create(&(t),NULL,(fn),(arg))==0)
#define THREAD_JOIN(t) pthread_join((t),NULL)
#define MKDIR(d) mkdir((d),0755)
#endif

#include <curl/curl.h>
#include <sqlite3.h>

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

static void free_ptr(char **p) { if (p && *p) { free(*p); *p = NULL; } }

/* ------------------------------------------------------------------ */
/* shared state                                                       */
/* ------------------------------------------------------------------ */

static sqlite3 *g_db = NULL;
static int g_db_ok = 0;

static mutex_t g_mutex;
static cond_t  g_cond;
static thread_t g_thread;
static int             g_thread_started = 0;
static int             g_stop = 0;

static int g_busy = 0;   /* 1 while a request is in flight */
static int g_done = 1;   /* 1 when a result is ready */
static int g_cancel = 0;

/* pending request (owned by the worker once consumed) */
static char *g_p_method = NULL;
static char *g_p_url = NULL;
static char *g_p_body = NULL;
static char *g_p_headers = NULL;
static long   g_p_timeout = 0;

/* last result */
static HttpResponse g_last = { 0, 0.0, NULL, 0, NULL };

/* history cache */
typedef struct { long id; char *method; char *url; char *created_at; int status; double latency_ms; char *req_headers; char *res_headers; } HistRec;
static HistRec *g_hist = NULL;
static int g_hist_n = 0;
static int g_hist_cap = 0;

/* ------------------------------------------------------------------ */
/* sqlite helpers                                                     */
/* ------------------------------------------------------------------ */

static void db_exec(const char *sql) {
    if (!g_db) return;
    char *err = NULL;
    sqlite3_exec(g_db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
}

static void db_migrate(void) {
    /* best-effort: add columns if missing */
    db_exec("ALTER TABLE requests ADD COLUMN req_headers TEXT;");
    db_exec("ALTER TABLE requests ADD COLUMN res_headers TEXT;");
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                         */
/* ------------------------------------------------------------------ */

int backend_init(const char *db_path) {
    static int initialized = 0;
    if (!initialized) {
        MUTEX_INIT(g_mutex);
        COND_INIT(g_cond);
        initialized = 1;
    }
    if (sqlite3_open(db_path, &g_db) != SQLITE_OK) {
        if (g_db) { sqlite3_close(g_db); g_db = NULL; }
        return -1;
    }
    db_exec("CREATE TABLE IF NOT EXISTS requests ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "created_at TEXT, method TEXT, url TEXT, req_body TEXT,"
            "status INTEGER, latency_ms REAL, res_size INTEGER,"
            "req_headers TEXT, res_headers TEXT);");
    db_exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT);");
    db_migrate();
    g_db_ok = 1;
    return 0;
}

void backend_shutdown(void) {
    /* stop worker */
    MUTEX_LOCK(g_mutex);
    g_stop = 1;
    COND_BROADCAST(g_cond);
    MUTEX_UNLOCK(g_mutex);
    if (g_thread_started) {
        THREAD_JOIN(g_thread);
        g_thread_started = 0;
    }
    /* free pending */
    free(g_p_method); free(g_p_url); free(g_p_body); free(g_p_headers);
    g_p_method = g_p_url = g_p_body = g_p_headers = NULL;
    /* free last result */
    free_response(&g_last);
    /* free history cache */
    for (int i = 0; i < g_hist_n; i++) {
        free(g_hist[i].method); free(g_hist[i].url); free(g_hist[i].created_at);
        free(g_hist[i].req_headers); free(g_hist[i].res_headers);
    }
    free(g_hist); g_hist = NULL; g_hist_n = g_hist_cap = 0;
    if (g_db) { sqlite3_close(g_db); g_db = NULL; }
    g_db_ok = 0;
}

/* ------------------------------------------------------------------ */
/* http helpers                                                      */
/* ------------------------------------------------------------------ */

typedef struct { char *data; size_t len; size_t cap; int cancel; } Buf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Buf *b = (Buf *)userdata;
    if (b->cancel) return 0;
    size_t n = size * nmemb;
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        char *nd = (char *)realloc(b->data, nc);
        if (!nd) return 0;
        b->data = nd; b->cap = nc;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Buf *b = (Buf *)userdata;
    if (b->cancel) return 0;
    size_t n = size * nmemb;
    /* append */
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n + 1) nc *= 2;
        char *nd = (char *)realloc(b->data, nc);
        if (!nd) return 0;
        b->data = nd; b->cap = nc;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/* cancel flag shared with worker via global g_cancel (checked in callbacks) */
static int *g_cancel_ptr = &g_cancel;

static size_t write_cb_cancel(char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (g_cancel) return 0;
    return write_cb(ptr, size, nmemb, userdata);
}
static size_t header_cb_cancel(char *ptr, size_t size, size_t nmemb, void *userdata) {
    if (g_cancel) return 0;
    return header_cb(ptr, size, nmemb, userdata);
}

static void do_http(const char *method, const char *url, const char *body,
                    const char *headers, long timeout_ms, HttpResponse *out) {
    memset(out, 0, sizeof *out);
    CURL *curl = curl_easy_init();
    if (!curl) {
        out->status_code = -1;
        out->body = xstrdup("curl_easy_init failed");
        out->body_len = (long)strlen(out->body);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    /* headers */
    struct curl_slist *hs = NULL;
    if (headers && headers[0]) {
        /* split on '\n' */
        char *copy = xstrdup(headers);
        char *line = copy;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            /* trim trailing \r */
            size_t L = strlen(line);
            while (L > 0 && (line[L-1] == '\r' || line[L-1] == ' ')) line[--L] = '\0';
            if (line[0]) hs = curl_slist_append(hs, line);
            if (nl) line = nl + 1; else break;
        }
        free(copy);
    }
    if (hs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hs);

    /* body */
    if (body && body[0]) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }

    if (timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    }

    Buf rb = { NULL, 0, 0, 0 };
    Buf rh = { NULL, 0, 0, 0 };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb_cancel);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rb);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb_cancel);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rh);

    /* user agent */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cui-http/1.0");

    clock_t t0 = clock();
    CURLcode rc = curl_easy_perform(curl);
    clock_t t1 = clock();
    double ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(hs);
    curl_easy_cleanup(curl);

    int cancelled = g_cancel;
    /* reset cancel flag */
    /* (caller/worker handles reset; do it here too) */

    if (rc != CURLE_OK) {
        out->status_code = cancelled ? -2 : -1;
        free(rb.data);
        char buf[256];
        snprintf(buf, sizeof buf, "%s", curl_easy_strerror(rc));
        rb.data = xstrdup(buf);
        rb.len = strlen(rb.data);
    } else {
        out->status_code = (int)code;
    }
    out->latency_ms = ms;
    out->body = rb.data ? rb.data : xstrdup("");
    out->body_len = rb.data ? (long)rb.len : 0;
    if (!rb.data) { out->body = xstrdup(""); out->body_len = 0; }
    /* ensure NUL */
    /* rb.data already NUL terminated by write_cb */
    out->headers = rh.data ? rh.data : NULL;
    /* ensure headers NUL terminated */
    if (rh.data && rh.len > 0 && rh.data[rh.len] != '\0') {
        /* realloc + NUL */
        char *nd = (char *)realloc(rh.data, rh.len + 1);
        if (nd) { nd[rh.len] = '\0'; out->headers = nd; }
    }

    g_cancel = 0;
}

/* ------------------------------------------------------------------ */
/* worker thread                                                      */
/* ------------------------------------------------------------------ */

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        MUTEX_LOCK(g_mutex);
        while (!g_busy && !g_stop) COND_WAIT(g_cond, g_mutex);
        if (g_stop) { MUTEX_UNLOCK(g_mutex); break; }

        /* copy pending */
        char *method = xstrdup(g_p_method ? g_p_method : "GET");
        char *url = xstrdup(g_p_url ? g_p_url : "");
        char *body = xstrdup(g_p_body ? g_p_body : "");
        char *headers = xstrdup(g_p_headers ? g_p_headers : "");
        long timeout = g_p_timeout;

        /* mark consumed (keep g_busy=1 until done) */
        MUTEX_UNLOCK(g_mutex);

        /* perform request outside lock */
        HttpResponse res;
        do_http(method, url, body, headers, timeout, &res);

        /* save to db */
        if (g_db_ok) {
            char *sql = NULL;
            /* use prepared statement */
            sqlite3_stmt *stmt = NULL;
            const char *q = "INSERT INTO requests (created_at, method, url, req_body, status, latency_ms, res_size, req_headers, res_headers) VALUES (?,?,?,?,?,?,?,?,?);";
            if (sqlite3_prepare_v2(g_db, q, -1, &stmt, NULL) == SQLITE_OK) {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                char ts[64];
                strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", tm);
                sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, method, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, url, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, body, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 5, res.status_code);
                sqlite3_bind_double(stmt, 6, res.latency_ms);
                sqlite3_bind_int64(stmt, 7, res.body_len);
                sqlite3_bind_text(stmt, 8, headers, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 9, res.headers ? res.headers : "", -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        /* store result */
        MUTEX_LOCK(g_mutex);
        free_response(&g_last);
        g_last = res;
        free(method); free(url); free(body); free(headers);
        g_busy = 0;
        g_done = 1;
        COND_BROADCAST(g_cond);
        MUTEX_UNLOCK(g_mutex);
    }
    return NULL;
}

#ifdef _WIN32
static DWORD WINAPI win_worker(LPVOID arg) {
    worker_main(arg);
    return 0;
}
#endif
/* ------------------------------------------------------------------ */

void start_request(const char *method, const char *url, const char *body,
                   const char *headers, long timeout_ms) {
    MUTEX_LOCK(g_mutex);
    if (g_busy) { MUTEX_UNLOCK(g_mutex); return; }
    free(g_p_method); free(g_p_url); free(g_p_body); free(g_p_headers);
    g_p_method = xstrdup(method ? method : "GET");
    g_p_url = xstrdup(url ? url : "");
    g_p_body = xstrdup(body ? body : "");
    g_p_headers = xstrdup(headers ? headers : "");
    g_p_timeout = timeout_ms;
    g_busy = 1;
    g_done = 0;
    g_cancel = 0;
    COND_SIGNAL(g_cond);
    MUTEX_UNLOCK(g_mutex);

    if (!g_thread_started) {
        g_thread_started = 1;
        g_stop = 0;
        THREAD_CREATE(g_thread, worker_main, NULL);
    }
}

int request_done(void) {
    MUTEX_LOCK(g_mutex);
    int d = g_done && !g_busy;
    MUTEX_UNLOCK(g_mutex);
    return d;
}

HttpResponse last_response(void) {
    MUTEX_LOCK(g_mutex);
    HttpResponse copy = g_last;
    /* deep copy body and headers */
    copy.body = g_last.body ? xstrdup(g_last.body) : NULL;
    copy.headers = g_last.headers ? xstrdup(g_last.headers) : NULL;
    MUTEX_UNLOCK(g_mutex);
    return copy;
}

void cancel_request(void) {
    MUTEX_LOCK(g_mutex);
    if (g_busy) g_cancel = 1;
    MUTEX_UNLOCK(g_mutex);
}

void free_response(HttpResponse *res) {
    if (!res) return;
    free(res->body); res->body = NULL;
    free(res->headers); res->headers = NULL;
}

/* ------------------------------------------------------------------ */
/* history                                                            */
/* ------------------------------------------------------------------ */

int history_refresh(void) {
    if (!g_db_ok) return -1;
    /* free cache */
    for (int i = 0; i < g_hist_n; i++) {
        free(g_hist[i].method); free(g_hist[i].url); free(g_hist[i].created_at);
        free(g_hist[i].req_headers); free(g_hist[i].res_headers);
    }
    g_hist_n = 0;

    const char *q = "SELECT id, method, url, created_at, status, latency_ms, req_headers, res_headers FROM requests ORDER BY id DESC LIMIT 500;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, q, -1, &stmt, NULL) != SQLITE_OK) return -1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (g_hist_n == g_hist_cap) {
            int nc = g_hist_cap ? g_hist_cap * 2 : 64;
            HistRec *nb = (HistRec *)realloc(g_hist, nc * sizeof(HistRec));
            if (!nb) { sqlite3_finalize(stmt); return -1; }
            g_hist = nb; g_hist_cap = nc;
        }
        HistRec *r = &g_hist[g_hist_n++];
        r->id = sqlite3_column_int64(stmt, 0);
        r->method = xstrdup((const char *)sqlite3_column_text(stmt, 1));
        r->url = xstrdup((const char *)sqlite3_column_text(stmt, 2));
        r->created_at = xstrdup((const char *)sqlite3_column_text(stmt, 3));
        r->status = sqlite3_column_int(stmt, 4);
        r->latency_ms = sqlite3_column_double(stmt, 5);
        const char *rh = (const char *)sqlite3_column_text(stmt, 6);
        const char *sh = (const char *)sqlite3_column_text(stmt, 7);
        r->req_headers = xstrdup(rh ? rh : "");
        r->res_headers = xstrdup(sh ? sh : "");
    }
    sqlite3_finalize(stmt);
    return 0;
}

int history_count(void) { return g_hist_n; }

int history_row(int i, HistoryRow *out_row) {
    if (i < 0 || i >= g_hist_n || !out_row) return -1;
    memset(out_row, 0, sizeof *out_row);
    HistRec *r = &g_hist[i];
    out_row->id = r->id;
    out_row->method = xstrdup(r->method);
    out_row->url = xstrdup(r->url);
    out_row->created_at = xstrdup(r->created_at);
    out_row->status = r->status;
    out_row->latency_ms = r->latency_ms;
    out_row->req_headers = xstrdup(r->req_headers ? r->req_headers : "");
    out_row->res_headers = xstrdup(r->res_headers ? r->res_headers : "");
    return 0;
}

void free_history_row(HistoryRow *row) {
    if (!row) return;
    free(row->method); free(row->url); free(row->created_at);
    free(row->req_headers); free(row->res_headers);
    memset(row, 0, sizeof *row);
}

void clear_history(void) {
    if (!g_db_ok) return;
    db_exec("DELETE FROM requests;");
    /* refresh cache */
    history_refresh();
}

void delete_history_row(long id) {
    if (!g_db_ok) return;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM requests WHERE id = ?;", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

/* ------------------------------------------------------------------ */
/* settings                                                          */
/* ------------------------------------------------------------------ */

char *get_setting(const char *key) {
    if (!g_db_ok || !key) return NULL;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, "SELECT value FROM settings WHERE key = ?;", -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    char *res = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(stmt, 0);
        res = xstrdup(v ? v : "");
    }
    sqlite3_finalize(stmt);
    return res;
}

void set_setting(const char *key, const char *value) {
    if (!g_db_ok || !key) return;
    sqlite3_stmt *stmt = NULL;
    const char *q = "INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
    if (sqlite3_prepare_v2(g_db, q, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

/* ------------------------------------------------------------------ */
/* default db path                                                   */
/* ------------------------------------------------------------------ */

char *default_db_path(void) {
#ifdef _WIN32
    const char *base = getenv("APPDATA");
    if (!base) base = getenv("LOCALAPPDATA");
    if (!base) base = ".";
    /* build path base/cui-http/requests.db */
    size_t n = strlen(base) + 32;
    char *dir = (char *)malloc(n);
    snprintf(dir, n, "%s\\cui-http", base);
#else
    const char *base = getenv("XDG_DATA_HOME");
    char dirbuf[4096];
    if (!base || !base[0]) {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        snprintf(dirbuf, sizeof dirbuf, "%s/.local/share", home);
        base = dirbuf;
    }
    char *dir = (char *)malloc(strlen(base) + 32);
    snprintf(dir, strlen(base) + 32, "%s/cui-http", base);
#endif
    /* create dir (best effort) */
    MKDIR(dir);
    size_t total = strlen(dir) + 32;
    char *outp = (char *)malloc(total);
#ifdef _WIN32
    snprintf(outp, total, "%s\\requests.db", dir);
#else
    snprintf(outp, total, "%s/requests.db", dir);
#endif
    free(dir);
    return outp;
}