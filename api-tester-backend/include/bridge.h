#ifndef BRIDGE_H
#define BRIDGE_H

/* C ABI bridge between the C frontend (source-frontend/main.c) and the D
 * backend (compiled into a static library by CMake).
 *
 * Memory contract: every char* handed to C is malloc'ed by the backend and
 * must be released from C with free_response() / free_string(). */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- D runtime lifecycle ---- */
bool initDRuntime(void);
bool terminateDRuntime(void);

/* ---- Persistence ---- */
/* Opens (or creates) the SQLite database. Schema: history + settings. */
int init_db(const char* db_path);

/* ---- Requests ---- */
typedef struct {
    int status_code;   /* HTTP code; -1 network error, -2 cancelled */
    double latency_ms;
    char* body;        /* malloc'ed, NUL terminated */
    long body_len;     /* bytes without the NUL */
    char* headers;     /* malloc'ed response headers, "Key: value\\n" per line, or NULL */
} HttpResponse;

typedef struct {
    long id;           /* row id in the requests table */
    char* method;
    char* url;
    char* created_at;  /* ISO-8601 timestamp */
    int status;
    double latency_ms;
    char* req_headers; /* malloc'ed, "Key: value\\n" per line, or NULL */
    char* res_headers; /* malloc'ed, "Key: value\\n" per line, or NULL */
} HistoryRow;

/* Ask the backend's worker thread to perform an HTTP request. Non blocking;
 * poll with request_done() and fetch the result with last_response().
 * `headers` is NULL, or one "Name: value" line per HTTP header. `timeout_ms`
 * limits the whole exchange at curl level (0 = no limit). */
void start_request_ex(const char* method, const char* url, const char* body, const char* headers, long timeout_ms);
void start_request(const char* method, const char* url, const char* body);
/* 1 once the last request finished (its response stays valid until then). */
int request_done(void);
/* Snapshot of the last response. Copy what you need and free below. */
HttpResponse last_response(void);
/* Cancel the in flight request (it ends with status_code -2). */
void cancel_request(void);
void free_response(HttpResponse* res);

/* Must be called once after initDRuntime() and before start_request(). */
void start_worker(void);
/* Orderly shutdown of the worker; call before terminateDRuntime(). */
void backend_shutdown(void);

/* ---- History ---- */
/* Re-reads the requests table into the backend's row cache. */
int history_refresh(void);
/* Number of cached rows (0 until history_refresh() succeeded). */
int history_count(void);
/* Fill *row with cached row i (0 = newest); malloc'ed strings; 0 = ok. */
int history_row(int i, HistoryRow* row);
void clear_history(void);
/* Delete one history row by its id. */
void delete_history_row(long id);

/* ---- Settings ---- */
/* Returns a malloc'ed value or NULL when the key is not set. */
char* get_setting(const char* key);
void set_setting(const char* key, const char* value);

/* Releases a malloc'ed string previously given by the backend. */
void free_string(char* s);

/* Returns a malloc'ed path to the OS-conventional location of the default
 * history database (e.g. $XDG_DATA_HOME/cui-http/requests.db or
 * $HOME/.local/share/cui-http/requests.db on Linux), creating the directory
 * when needed. The caller frees the result with free_string(). */
char* default_db_path(void);

#ifdef __cplusplus
}
#endif

#endif
