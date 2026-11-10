/* backend.d - D backend for cui-http.
 *
 * Compiled by CMake into a static library (ldc2 -lib) and linked into the C
 * frontend (see CMakeLists.txt). It provides:
 *
 *   - D runtime lifecycle (initDRuntime / terminateDRuntime)
 *   - SQLite persistence (history of requests + settings) via d2sqlite3
 *   - An HTTP engine backed by libcurl (std.net.curl), run asynchronously
 *     on a worker thread owned by the D runtime, with cancellation support
 *   - A C ABI bridge consumed through api-tester-backend/include/bridge.h
 *
 * All memory crossing the C boundary is allocated with malloc() and freed
 * from C (free_response / free_string), so nothing GC-owned escapes. */
module backend;

import core.runtime;
import core.thread;
import core.time : msecs;
import core.atomic : atomicLoad, atomicStore;
import core.stdc.stdlib : malloc, free;
import core.stdc.string : memcpy;
import std.algorithm.iteration : splitter;
import std.string : indexOf, strip;
import std.datetime.stopwatch : StopWatch, AutoStart;
import std.datetime.systime : Clock;
import std.net.curl : HTTP;
import etc.c.curl : CurlReadFunc;
import d2sqlite3;
import std.conv : to;

/* ------------------------------------------------------------------------ */
/* D runtime lifecycle (called from the C frontend's main)                   */
/* ------------------------------------------------------------------------ */

extern (C) bool initDRuntime() {
    return Runtime.initialize();
}

extern (C) bool terminateDRuntime() {
    return Runtime.terminate();
}

/* ------------------------------------------------------------------------ */
/* Shared structs: keep field order and types in sync with bridge.h          */
/* ------------------------------------------------------------------------ */

/* Result of an HTTP exchange. status_code is the HTTP code, or:
 *   -1: network error, -2: request cancelled by the user. */
extern (C) struct HttpResponse {
    int status_code;
    double latency_ms;
    char* body;    /* malloc'ed, NUL terminated; freed with free_response */
    long body_len; /* bytes without the NUL */
}

/* One history row, as returned by history_row(). Strings are malloc'ed. */
extern (C) struct HistoryRow {
    long id;
    char* method;
    char* url;
    char* created_at;
    int status;
    double latency_ms;
}

/* ------------------------------------------------------------------------ */
/* Internal state                                                            */
/* ------------------------------------------------------------------------ */

private __gshared Database db;
private __gshared bool dbInitialized = false;
private __gshared Object dbLock;
private __gshared HistoryRec[] gHist; /* cache for history_row(); 0 = newest */

private __gshared Thread gWorker;
private __gshared bool gWorkerStarted = false;
private __gshared shared(bool) gRunning = false;
private __gshared shared(bool) gHasWork = false;
private __gshared shared(bool) gDone = true;
private __gshared shared(bool) gCancel = false;
private __gshared string gPendingMethod, gPendingUrl, gPendingBody;
private __gshared string[] gPendingHeaders;
private __gshared long gPendingTimeoutMs = 0;
private __gshared RawResponse gLastRaw = { 0, 0.0, null };

private struct HistoryRec {
    long id;
    string method;
    string url;
    string created_at;
    int status;
    double latency_ms;
}

/* Copy a GC string into malloc'ed, NUL terminated C memory. */
private char* copy_to_cstring(string s) {
    auto p = cast(char*) malloc(s.length + 1);
    if (p !is null) {
        memcpy(p, s.ptr, s.length);
        p[s.length] = '\0';
    }
    return p;
}

/* ------------------------------------------------------------------------ */
/* Persistence: history + settings                                           */
/* ------------------------------------------------------------------------ */

extern (C) int init_db(const char* c_db_path) {
    try {
        dbLock = new Object;
        synchronized (dbLock) {
            db = Database(to!string(c_db_path));
            db.run("CREATE TABLE IF NOT EXISTS requests (
                       id INTEGER PRIMARY KEY AUTOINCREMENT,
                       created_at TEXT,
                       method TEXT,
                       url TEXT,
                       req_body TEXT,
                       status INTEGER,
                       latency_ms REAL,
                       res_size INTEGER)");
            db.run("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)");
            dbInitialized = true;
        }
        return 0;
    } catch (Exception e) {
        return -1;
    }
}

extern (C) int history_refresh() {
    if (!dbInitialized) return -1;
    try {
        synchronized (dbLock) {
            gHist.length = 0;
            auto stmt = db.prepare("SELECT id, method, url, created_at, status, latency_ms
                                    FROM requests ORDER BY id DESC LIMIT 500");
            foreach (row; stmt.execute()) {
                HistoryRec rec;
                rec.id = row.peek!long(0);
                rec.method = row.peek!string(1);
                rec.url = row.peek!string(2);
                rec.created_at = row.peek!string(3);
                rec.status = cast(int) row.peek!long(4);
                rec.latency_ms = row.peek!double(5);
                gHist ~= rec;
            }
        }
        return 0;
    } catch (Exception e) {
        return -1;
    }
}

extern (C) int history_count() {
    return cast(int) gHist.length;
}

/* Row index counts from the newest (0). Returns 0 on success, -1 on range error. */
extern (C) int history_row(int i, HistoryRow* out_row) {
    if (i < 0 || i >= gHist.length) return -1;
    auto rec = gHist[i];
    out_row.id = rec.id;
    out_row.method = copy_to_cstring(rec.method);
    out_row.url = copy_to_cstring(rec.url);
    out_row.created_at = copy_to_cstring(rec.created_at);
    out_row.status = rec.status;
    out_row.latency_ms = rec.latency_ms;
    return (out_row.method && out_row.url && out_row.created_at) ? 0 : -1;
}

extern (C) void clear_history() {
    if (!dbInitialized) return;
    try {
        synchronized (dbLock) {
            db.run("DELETE FROM requests");
        }
        gHist.length = 0;
    } catch (Exception e) {
    }
}

extern (C) char* get_setting(const char* c_key) {
    if (!dbInitialized) return null;
    try {
        synchronized (dbLock) {
            auto stmt = db.prepare("SELECT value FROM settings WHERE key = ?");
            stmt.bind(1, to!string(c_key));
            foreach (row; stmt.execute()) {
                return copy_to_cstring(row.peek!string(0));
            }
        }
    } catch (Exception e) {
    }
    return null;
}

extern (C) void set_setting(const char* c_key, const char* c_value) {
    if (!dbInitialized) return;
    try {
        synchronized (dbLock) {
            auto stmt = db.prepare("INSERT INTO settings (key, value) VALUES (?, ?)
                                    ON CONFLICT(key) DO UPDATE SET value = excluded.value");
            stmt.bind(1, to!string(c_key));
            stmt.bind(2, to!string(c_value));
            stmt.execute();
        }
    } catch (Exception e) {
    }
}

/* ------------------------------------------------------------------------ */
/* HTTP engine                                                               */
/* ------------------------------------------------------------------------ */

private HTTP.Method map_method(string method) {
    switch (method) {
        case "GET": return HTTP.Method.get;
        case "POST": return HTTP.Method.post;
        case "PUT": return HTTP.Method.put;
        case "DELETE": return HTTP.Method.del;
        case "HEAD": return HTTP.Method.head;
        case "OPTIONS": return HTTP.Method.options;
        case "TRACE": return HTTP.Method.trace;
        case "CONNECT": return HTTP.Method.connect;
        case "PATCH": return HTTP.Method.patch;
        default: return HTTP.Method.undefined;
    }
}

/* Internal response owned by the GC: the backend keeps it in gLastRaw and
 * never releases malloc'ed memory of its own for it. */
private struct RawResponse {
    int status_code;
    double latency_ms;
    string body;
}

private RawResponse run_http(string method, string url, string body, string[] headers, long timeout_ms) {
    RawResponse res = { -1, 0.0, null };
    auto sw = StopWatch(AutoStart.yes);
    string received;

    try {
        /* HTTP is a struct: the value constructor (not `new`) runs
         * initialize() and creates the curl handle. */
        auto client = HTTP();
        client.url = url;
        client.method = map_method(method);
        foreach (h; headers) {
            auto sep = h.indexOf(": ");
            if (sep > 0) {
                client.addRequestHeader(h[0 .. sep].strip, h[sep + 2 .. $].strip);
            }
        }
        bool wantsBody = client.method != HTTP.Method.get &&
                         client.method != HTTP.Method.head &&
                         body.length > 0;
        if (wantsBody) {
            import std.algorithm.comparison : min;
            client.contentLength = body.length;
            ubyte[] remaining = cast(ubyte[]) body;
            client.onSend = delegate size_t(void[] buf) {
                size_t n = min(buf.length, remaining.length);
                if (n == 0) return 0;
                buf[0 .. n] = remaining[0 .. n];
                remaining = remaining[n .. $];
                return n;
            };
        }
        if (timeout_ms > 0) {
            /* curl level timeout (CURLOPT_TIMEOUT), so idle connections die too */
            client.operationTimeout(timeout_ms.msecs);
        }
        client.onReceive = delegate size_t(ubyte[] data) {
            if (atomicLoad(gCancel)) return CurlReadFunc.abort;
            received ~= cast(string) data;
            return data.length;
        };
        client.perform();
        sw.stop();
        res.status_code = cast(int) client.statusLine.code;
    } catch (Exception e) {
        sw.stop();
        res.status_code = -1;
        received = atomicLoad(gCancel) ? "Request cancelled" : "Error: " ~ e.msg;
    }

    if (res.status_code <= 0) {
        /* the message discriminates a user cancel from a network error */
        res.status_code = atomicLoad(gCancel) ? -2 : -1;
    }
    res.latency_ms = sw.peek.total!"nsecs" / 1_000_000.0;
    res.body = received;
    atomicStore(gCancel, false);
    return res;
}

/* Persist one request/response pair to the history table. */
private void save_request(string method, string url, string body, RawResponse res) {
    if (!dbInitialized || res.status_code == 0) return;
    try {
        synchronized (dbLock) {
            auto stmt = db.prepare("INSERT INTO requests
                (created_at, method, url, req_body, status, latency_ms, res_size)
                VALUES (?, ?, ?, ?, ?, ?, ?)");
            stmt.bind(1, Clock.currTime().toISOExtString());
            stmt.bind(2, method);
            stmt.bind(3, url);
            stmt.bind(4, body.length > 65536 ? body[0 .. 65536] : body);
            stmt.bind(5, res.status_code);
            stmt.bind(6, res.latency_ms);
            stmt.bind(7, cast(long) res.body.length);
            stmt.execute();
        }
    } catch (Exception e) {
        /* Persistence failures must not break the request flow. */
    }
}

/* Worker thread: polls the pending request flag and executes requests.
 * Built with core.thread.Thread so the D GC knows about this thread. */
private void worker_loop() {
    while (atomicLoad(gRunning)) {
        if (!atomicLoad(gHasWork)) {
            Thread.sleep(5.msecs);
            continue;
        }
        string method = gPendingMethod;
        string url = gPendingUrl;
        string body = gPendingBody;
        string[] headers = gPendingHeaders;
        auto res = run_http(method, url, body, headers, gPendingTimeoutMs);
        atomicStore(gHasWork, false);
        save_request(method, url, body, res);
        gLastRaw = res;
        atomicStore(gDone, true);
    }
}

/* Kick off an asynchronous request. Flags are ordered so the worker only
 * observes the request after the completion of both stores. `c_headers` is
 * a NUL terminated block with one "Name: value" header per line, or NULL.
 * `timeout_ms` limits the whole exchange at curl level (0 = no limit). */
extern (C) void start_request_ex(const char* c_method, const char* c_url,
                                 const char* c_body, const char* c_headers,
                                 long timeout_ms) {
    if (atomicLoad(gHasWork)) return; /* one request at a time */

    /* Store the request, then raise the flag. */
    gPendingMethod = to!string(c_method);
    gPendingUrl = to!string(c_url);
    gPendingBody = c_body !is null ? to!string(c_body) : "";
    gPendingTimeoutMs = timeout_ms;

    gPendingHeaders.length = 0;
    if (c_headers !is null) {
        foreach (line; to!string(c_headers).splitter('\n')) {
            string h = line.strip;
            if (h.length) gPendingHeaders ~= h;
        }
    }

    atomicStore(gDone, false);
    atomicStore(gHasWork, true);
}

extern (C) void start_request(const char* c_method, const char* c_url, const char* c_body) {
    start_request_ex(c_method, c_url, c_body, null, 0);
}

extern (C) int request_done() {
    return atomicLoad(gDone) ? 1 : 0;
}

/* Snapshot of the last response into fresh malloc'ed memory: C owns the copy
 * and frees it with free_response(); the backend keeps nothing malloc'ed. */
extern (C) HttpResponse last_response() {
    HttpResponse res;
    res.status_code = gLastRaw.status_code;
    res.latency_ms = gLastRaw.latency_ms;
    res.body_len = cast(long) gLastRaw.body.length;
    res.body = copy_to_cstring(gLastRaw.body);
    return res;
}

extern (C) void cancel_request() {
    if (atomicLoad(gHasWork)) atomicStore(gCancel, true);
}

extern (C) void free_response(HttpResponse* res) {
    if (res !is null && res.body !is null) {
        free(res.body);
        res.body = null;
    }
}

extern (C) void free_string(char* s) {
    if (s !is null) free(s);
}

/* Accepted the pending request; do not lose the worker running the GC. */
extern (C) void start_worker() {
    if (gWorkerStarted) return;
    gWorkerStarted = true;
    atomicStore(gRunning, true);
    auto t = new Thread(&worker_loop, 512 * 1024);
    t.start();
    gWorker = t; /* keep a reference: the GC must not collect it */
}

/* Orderly shutdown at spawn: stop the worker. There is nothing malloc'ed to
 * release: response bodies handed to C are owned by C. */
extern (C) void backend_shutdown() {
    if (gWorkerStarted) {
        atomicStore(gRunning, false);
        if (gWorker !is null && gWorker.isRunning) {
            try {
                gWorker.join();
            } catch (Exception e) {
            }
        }
        gWorker = null;
        gWorkerStarted = false;
    }
}
