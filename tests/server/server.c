/* server.c - tiny local REST API in pure C99 to test cui-http end to end.
 *
 * Endpoints:
 *   GET    /users            list users
 *   GET    /users/:id        one user (404 when missing)
 *   POST   /users            create: body {"name": "...", "email": "..."} -> 201
 *   PUT    /users/:id        replace -> 200
 *   PATCH  /users/:id        partial update -> 200
 *   DELETE /users/:id        delete -> 204
 *   OPTIONS /users           method round trip (Allow header)
 *   HEAD   /users            method round trip (no body)
 *   GET    /echo             echoes method, body and all request headers
 *   GET    /status/:code     answers with that HTTP status
 *   GET    /slow?ms=NNN      waits NNN ms before answering
 *
 * Build:  gcc -O2 -std=c99 -o tests/server/bin/cui-http-test-server tests/server/server.c -lpthread
 * Run:    tests/server/bin/cui-http-test-server   (port 8090) */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8090
#define BUF 65536

typedef struct { long id; char name[128]; char email[128]; } User;

static User g_users[64];
static int g_nusers = 0;
static long g_nextid = 1;

static void seed(void) {
    g_users[0].id = 1; strcpy(g_users[0].name, "Ada Lovelace"); strcpy(g_users[0].email, "ada@example.com");
    g_users[1].id = 2; strcpy(g_users[1].name, "Linus T."); strcpy(g_users[1].email, "linus@example.com");
    g_nusers = 2;
    g_nextid = 3;
}

static User *find_user(long id) {
    for (int i = 0; i < g_nusers; i++) if (g_users[i].id == id) return &g_users[i];
    return NULL;
}

static void send_raw(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = send(fd, data + off, len - off, 0);
        if (w <= 0) break;
        off += (size_t)w;
    }
}

static void respond(int fd, int code, const char *ctype, const char *body, const char *extra_headers) {
    char head[1024];
    size_t blen = body ? strlen(body) : 0;
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        code, (code==200?"OK":code==201?"Created":code==204?"No Content":code==400?"Bad Request":code==404?"Not Found":"Error"),
        ctype ? ctype : "text/plain", blen, extra_headers ? extra_headers : "");
    send_raw(fd, head, (size_t)hl);
    if (body && blen) send_raw(fd, body, blen);
}

static void handle(int fd, const char *req) {
    /* parse request line */
    char method[16] = {0}, path[512] = {0};
    sscanf(req, "%15s %511s", method, path);

    /* split path and query */
    char *qmark = strchr(path, '?');
    char query[512] = {0};
    if (qmark) { snprintf(query, sizeof query, "%s", qmark+1); *qmark = '\0'; }

    /* headers: collect into a buffer for echo; find body */
    /* body starts after \r\n\r\n */
    const char *body = strstr(req, "\r\n\r\n");
    if (body) body += 4; else body = "";

    /* content-length */
    int clen = 0;
    const char *cl = strcasestr(req, "Content-Length:");
    if (cl) sscanf(cl, "Content-Length: %d", &clen);
    /* ensure body null terminated at clen */
    /* (req buffer is large; we'll just use clen) */

    /* helper to get header value */
    /* (not needed for routing) */

    /* ---- routing ---- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/users") == 0) {
        /* build JSON list */
        char buf[BUF]; size_t off = 0;
        off += snprintf(buf + off, BUF - off, "[");
        for (int i = 0; i < g_nusers; i++) {
            if (i) off += snprintf(buf + off, BUF - off, ",");
            off += snprintf(buf + off, BUF - off, "{\"id\":%ld,\"name\":\"%s\",\"email\":\"%s\"}",
                            g_users[i].id, g_users[i].name, g_users[i].email);
        }
        off += snprintf(buf + off, BUF - off, "]");
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/users/", 7) == 0) {
        long id = atol(path + 7);
        User *u = find_user(id);
        if (!u) { respond(fd, 404, "application/json", "{\"error\":\"user not found\"}", NULL); return; }
        char buf[256];
        snprintf(buf, sizeof buf, "{\"id\":%ld,\"name\":\"%s\",\"email\":\"%s\"}", u->id, u->name, u->email);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/users") == 0) {
        /* reject obviously invalid JSON */
        const char *t = body; while (*t==' '||*t=='\t'||*t=='\n'||*t=='\r') t++;
        if (*t != '{') { respond(fd, 400, "application/json", "{\"error\":\"invalid JSON\"}", NULL); return; }
        /* parse name/email from body (naive) */
        char name[128] = {0}, email[128] = {0};
        const char *p = strstr(body, "\"name\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; size_t i=0; while (*p && *p!='"' && i<127) name[i++]=*p++; name[i]='\0'; } }
        p = strstr(body, "\"email\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; size_t i=0; while (*p && *p!='"' && i<127) email[i++]=*p++; email[i]='\0'; } }
        User u; u.id = g_nextid++; snprintf(u.name, sizeof u.name, "%s", name); snprintf(u.email, sizeof u.email, "%s", email);
        g_users[g_nusers++] = u;
        char buf[256];
        snprintf(buf, sizeof buf, "{\"created\":true,\"id\":%ld,\"name\":\"%s\",\"email\":\"%s\"}", u.id, u.name, u.email);
        respond(fd, 201, "application/json", buf, NULL);
    } else if (strcmp(method, "PUT") == 0 && strncmp(path, "/users/", 7) == 0) {
        long id = atol(path + 7);
        User *u = find_user(id);
        if (!u) { respond(fd, 404, "application/json", "{\"error\":\"user not found\"}", NULL); return; }
        char name[128]={0}, email[128]={0};
        const char *p = strstr(body, "\"name\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; size_t i=0; while (*p && *p!='"' && i<127) name[i++]=*p++; name[i]='\0'; } }
        p = strstr(body, "\"email\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; size_t i=0; while (*p && *p!='"' && i<127) email[i++]=*p++; email[i]='\0'; } }
        snprintf(u->name, sizeof u->name, "%s", name);
        snprintf(u->email, sizeof u->email, "%s", email);
        char buf[256];
        snprintf(buf, sizeof buf, "{\"updated\":true,\"id\":%ld,\"name\":\"%s\",\"email\":\"%s\"}", u->id, u->name, u->email);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "PATCH") == 0 && strncmp(path, "/users/", 7) == 0) {
        long id = atol(path + 7);
        User *u = find_user(id);
        if (!u) { respond(fd, 404, "application/json", "{\"error\":\"user not found\"}", NULL); return; }
        /* partial: name */
        const char *p = strstr(body, "\"name\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; char tmp[128]={0}; size_t i=0; while (*p && *p!='"' && i<127) tmp[i++]=*p++; tmp[i]='\0'; snprintf(u->name, sizeof u->name, "%s", tmp); } }
        p = strstr(body, "\"email\"");
        if (p) { p = strchr(p, ':'); if (p) { p++; while (*p==' '||*p=='"') p++; char tmp[128]={0}; size_t i=0; while (*p && *p!='"' && i<127) tmp[i++]=*p++; tmp[i]='\0'; snprintf(u->email, sizeof u->email, "%s", tmp); } }
        char buf[256];
        snprintf(buf, sizeof buf, "{\"patched\":true,\"id\":%ld,\"name\":\"%s\",\"email\":\"%s\"}", u->id, u->name, u->email);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/users/", 7) == 0) {
        long id = atol(path + 7);
        int removed = 0;
        for (int i = 0; i < g_nusers; i++) {
            if (g_users[i].id == id) {
                removed = 1;
                for (int j = i; j < g_nusers - 1; j++) g_users[j] = g_users[j+1];
                g_nusers--;
                break;
            }
        }
        if (!removed) { respond(fd, 404, "application/json", "{\"error\":\"user not found\"}", NULL); return; }
        respond(fd, 204, "application/json", "", NULL);
    } else if (strcmp(method, "OPTIONS") == 0 && strcmp(path, "/users") == 0) {
        respond(fd, 200, "application/json", "", "Allow: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n");
    } else if (strcmp(method, "HEAD") == 0 && strcmp(path, "/users") == 0) {
        respond(fd, 200, "application/json", "", NULL);
    } else if (strcmp(path, "/echo") == 0) {
        /* extract X-Token header value (if present) */
        char token[128] = {0};
        const char *ht = strcasestr(req, "X-Token:");
        if (ht) {
            ht += 8;
            while (*ht == ' ') ht++;
            size_t ti = 0;
            while (*ht && *ht != '\r' && *ht != '\n' && ti < sizeof(token)-1) token[ti++] = *ht++;
            token[ti] = '\0';
        }
        char buf[512];
        snprintf(buf, sizeof buf, "{\"method\":\"%s\",\"body\":\"%s\",\"token\":\"%s\"}", method, body, token);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "OPTIONS") == 0 && strcmp(path, "/users") == 0) {
    } else if (strcmp(method, "OPTIONS") == 0 && strcmp(path, "/users") == 0) {
        respond(fd, 200, "application/json", "", "Allow: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n");
    } else if (strcmp(method, "HEAD") == 0 && strcmp(path, "/users") == 0) {
        respond(fd, 200, "application/json", "", NULL);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/echo") == 0) {
        /* extract headers block */
        char rawhdrs[4096] = {0};
        const char *hs = strstr(req, "\r\n");
        if (hs) {
            hs += 2;
            const char *endh = strstr(hs, "\r\n\r\n");
            if (endh) {
                size_t hl = (size_t)(endh - hs);
                if (hl > sizeof(rawhdrs)-1) hl = sizeof(rawhdrs)-1;
                memcpy(rawhdrs, hs, hl); rawhdrs[hl]='\0';
            }
        }
        /* escape for JSON string */
        char esc[8192] = {0}; size_t eo = 0;
        for (size_t i = 0; rawhdrs[i] && eo < sizeof(esc)-2; i++) {
            if (rawhdrs[i] == '"') { esc[eo++]='\\'; esc[eo++]='"'; }
            else if (rawhdrs[i] == '\n') { esc[eo++]='\\'; esc[eo++]='n'; }
            else if (rawhdrs[i] == '\r') { /* skip */ }
            else esc[eo++]=rawhdrs[i];
        }
        esc[eo]='\0';
        char buf[BUF];
        snprintf(buf, sizeof buf, "{\"method\":\"GET\",\"body\":\"%s\",\"headers\":\"%s\"}", body, esc);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/echo") == 0) {
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/echo") == 0) {
        /* echo POST too */
        char buf[BUF];
        snprintf(buf, sizeof buf, "{\"method\":\"POST\",\"body\":\"%s\",\"headers\":\"\"}", body);
        respond(fd, 200, "application/json", buf, NULL);
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/status/", 8) == 0) {
        int code = atoi(path + 8);
        char buf[128];
        snprintf(buf, sizeof buf, "{\"status\":\"%d\"}", code);
        respond(fd, code, "application/json", buf, NULL);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/slow") == 0) {
        int ms = 200;
        char *m = strstr(query, "ms=");
        if (m) ms = atoi(m + 3);
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        char buf[128];
        snprintf(buf, sizeof buf, "{\"slow\":%d}", ms);
        respond(fd, 200, "application/json", buf, NULL);
    } else {
        respond(fd, 404, "application/json", "{\"error\":\"not found\"}", NULL);
    }
}

static void handle_conn(int fd) {
    char *buf = (char *)malloc(BUF);
    size_t total = 0;
    /* read until \r\n\r\n or EOF or full buffer */
    while (total < BUF - 1) {
        ssize_t r = recv(fd, buf + total, BUF - 1 - total, 0);
        if (r <= 0) break;
        total += (size_t)r;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) {
            /* check content-length to read full body */
            const char *cl = strcasestr(buf, "Content-Length:");
            int clen = 0;
            if (cl) sscanf(cl, "Content-Length: %d", &clen);
            const char *b = strstr(buf, "\r\n\r\n") + 4;
            size_t have = total - (size_t)(b - buf);
            if ((int)have >= clen) break;
        }
    }
    buf[total] = '\0';
    handle(fd, buf);
    free(buf);
    close(fd);
}

int main(void) {
    seed();
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }
    fprintf(stderr, "test server listening on 127.0.0.1:%d\n", PORT);
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        handle_conn(c);
    }
    close(srv);
    return 0;
}