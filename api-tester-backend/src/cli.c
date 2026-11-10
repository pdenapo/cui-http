/* cli.c - curl-like command line client for cui-http.
 * Reuses the same C backend, so requests land in the same SQLite history. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

static const char *USAGE =
"Usage: cui-http-cli [OPTIONS] [METHOD] URL [BODY]\n"
"\n"
"Options:\n"
"  -X, --method METHOD   HTTP method (default: GET)\n"
"  -d, --data BODY       request body (implies POST unless -X is given)\n"
"  -H, --header \"K: V\"   extra request header, repeatable\n"
"  --db PATH             history database file\n"
"  --timeout SECS        give up after this many seconds\n"
"\n"
"Commands:\n"
"  history [COUNT]       show the last rows (default 20)\n"
"  clear                 erase the whole history\n"
"  help                  show this text\n";

static int is_method(const char *s) {
    const char *ms[] = {"GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS"};
    for (int i = 0; i < 7; i++) if (strcasecmp(ms[i], s) == 0) return 1;
    return 0;
}

static char *join_headers(char **arr, int n) {
    if (n == 0) return NULL;
    size_t total = 0;
    for (int i = 0; i < n; i++) total += strlen(arr[i]) + 1;
    char *out = (char *)malloc(total + 1);
    out[0] = '\0';
    size_t off = 0;
    for (int i = 0; i < n; i++) {
        size_t L = strlen(arr[i]);
        memcpy(out + off, arr[i], L);
        off += L;
        out[off++] = '\n';
    }
    out[off] = '\0';
    return out;
}

static int run_request(const char *method, const char *url, const char *body,
                       char **headers, int nheaders, double timeout) {
    if (!url || !url[0]) { fprintf(stderr, "error: empty URL\n"); return 2; }
    char *hblock = join_headers(headers, nheaders);
    long to_ms = timeout > 0 ? (long)(timeout * 1000) : 0;
    start_request(method, url, body && body[0] ? body : NULL, hblock, to_ms);
    int ticks = 0;
    while (!request_done()) { /* busy wait */ struct timespec ts = {0, 20000000}; nanosleep(&ts, NULL); ticks++; }
    HttpResponse res = last_response();
    if (res.status_code == -1) fprintf(stderr, "error: network failure\n");
    else if (res.status_code == -2) fprintf(stderr, "cancelled\n");
    else printf("status: %d  (%.1f ms, %ld bytes)\n", res.status_code, res.latency_ms, res.body_len);
    if (res.body_len > 0 && res.body) {
        fwrite(res.body, 1, (size_t)res.body_len, stdout);
        putchar('\n');
    }
    free_response(&res);
    free(hblock);
    return (res.status_code >= 200 && res.status_code < 400) ? 0 : 1;
}

static int print_history(int count) {
    if (history_refresh() != 0) { fprintf(stderr, "error: cannot read history\n"); return 1; }
    int n = history_count();
    if (n > count) n = count;
    if (!n) { printf("(history is empty)\n"); return 0; }
    for (int i = 0; i < n; i++) {
        HistoryRow row;
        if (history_row(i, &row) != 0) break;
        printf("#%-5ld %4d %8.1fms %.19s  %-7s %s\n",
               row.id, row.status, row.latency_ms,
               row.created_at ? row.created_at : "",
               row.method ? row.method : "?",
               row.url ? row.url : "");
        free_history_row(&row);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *method = NULL;
    const char *db_path = NULL;
    const char *body = NULL;
    char **headers = NULL;
    int nheaders = 0, hcap = 0;
    double timeout = 0;

    /* parse args */
    int i = 1;
    /* collect positional */
    char **pos = NULL; int npos = 0, pcapt = 0;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-X") == 0 || strcmp(argv[i], "--method") == 0) {
            if (i + 1 < argc) method = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0) {
            if (i + 1 < argc) body = argv[++i];
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--header") == 0) {
            if (i + 1 < argc) {
                if (nheaders == hcap) { hcap = hcap ? hcap*2 : 8; headers = (char**)realloc(headers, hcap*sizeof(char*)); }
                headers[nheaders++] = argv[++i];
            }
        } else if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 < argc) db_path = argv[++i];
        } else if (strncmp(argv[i], "--db=", 5) == 0) {
            db_path = argv[i] + 5;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) timeout = atof(argv[++i]);
        } else if (strcmp(argv[i], "help") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fputs(USAGE, stdout);
            return 0;
        } else if (strcmp(argv[i], "history") == 0 || strcmp(argv[i], "clear") == 0) {
            /* subcommand: handle below after db open */
            /* store in pos[0] */
            if (npos == pcapt) { pcapt = pcapt ? pcapt*2 : 4; pos = (char**)realloc(pos, pcapt*sizeof(char*)); }
            pos[npos++] = argv[i];
            /* grab optional count */
            if (i + 1 < argc && (pos[0][0] == 'h')) {
                /* history [COUNT] */
                /* leave next token; handle later */
            }
        } else {
            if (npos == pcapt) { pcapt = pcapt ? pcapt*2 : 4; pos = (char**)realloc(pos, pcapt*sizeof(char*)); }
            pos[npos++] = argv[i];
        }
    }

    /* resolve db path */
    char dbbuf[4096];
    if (db_path && db_path[0]) {
        snprintf(dbbuf, sizeof dbbuf, "%s", db_path);
    } else {
        const char *env = getenv("CUI_HTTP_DB");
        if (env && env[0]) snprintf(dbbuf, sizeof dbbuf, "%s", env);
        else {
            char *def = default_db_path();
            snprintf(dbbuf, sizeof dbbuf, "%s", def ? def : "requests.db");
            free(def);
        }
    }

    if (backend_init(dbbuf) != 0) {
        fprintf(stderr, "error: cannot open database %s\n", dbbuf);
        return 1;
    }

    /* subcommands */
    if (npos > 0 && strcmp(pos[0], "clear") == 0) {
        clear_history();
        printf("history cleared\n");
        backend_shutdown();
        return 0;
    }
    if (npos > 0 && strcmp(pos[0], "history") == 0) {
        int count = 20;
        if (npos > 1) count = atoi(pos[1]);
        int rc = print_history(count);
        backend_shutdown();
        return rc;
    }

    /* request mode: [METHOD] URL [BODY] */
    char *methodpos = NULL;
    char **rest = pos;
    int nrest = npos;
    if (nrest > 0 && is_method(rest[0])) {
        methodpos = rest[0];
        rest++; nrest--;
    }
    if (nrest == 0) {
        fputs(USAGE, stdout);
        backend_shutdown();
        return 0;
    }
    const char *url = rest[0];
    const char *bodypos = (nrest > 1) ? rest[1] : NULL;

    if (bodypos && bodypos[0] && !body) body = bodypos;
    if (body && body[0] && !method && !methodpos) method = "POST";
    if (!method) method = methodpos ? methodpos : "GET";
    /* uppercase method */
    /* (leave as-is; backend handles case via? backend uses exact method string) */
    /* normalize to uppercase */
    static char mbuf[16];
    snprintf(mbuf, sizeof mbuf, "%s", method);
    for (char *c = mbuf; *c; c++) { if (*c >= 'a' && *c <= 'z') *c -= 32; }
    method = mbuf;

    start_worker_thread: ;
    /* start worker: backend_init doesn't start worker; ensure started */
    /* backend has no separate start_worker in C version? we auto-start in start_request */
    int rc = run_request(method, url, body, headers, nheaders, timeout);
    backend_shutdown();
    return rc;
}