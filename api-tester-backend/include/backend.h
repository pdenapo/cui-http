#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>

typedef struct {
    int status_code;   /* HTTP code; -1 network error, -2 cancelled */
    double latency_ms;
    char *body;        /* malloc'ed, NUL terminated */
    long body_len;     /* bytes without the NUL */
    char *headers;     /* malloc'ed response headers, "Key: value\r\n" per line, or NULL */
} HttpResponse;

typedef struct {
    long id;
    char *method;
    char *url;
    char *created_at;
    int status;
    double latency_ms;
    char *req_headers;
    char *res_headers;
} HistoryRow;

/* lifecycle */
int  backend_init(const char *db_path);   /* 0 ok */
void backend_shutdown(void);

/* async request */
void start_request(const char *method, const char *url, const char *body,
                   const char *headers, long timeout_ms);
int  request_done(void);
HttpResponse last_response(void);        /* caller frees body/headers with free_response */
void cancel_request(void);
void free_response(HttpResponse *res);

/* history */
int  history_refresh(void);
int  history_count(void);
int  history_row(int i, HistoryRow *out_row);   /* 0 ok */
void clear_history(void);
void delete_history_row(long id);
void free_history_row(HistoryRow *row);

/* settings */
char *get_setting(const char *key);   /* malloc'ed or NULL */
void set_setting(const char *key, const char *value);

/* default db path (malloc'ed) */
char *default_db_path(void);

#endif