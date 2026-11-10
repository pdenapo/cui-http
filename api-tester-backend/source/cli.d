/* cli.d - curl-like command line client for cui-http. It reuses the very same
 * backend (async HTTP engine + SQLite history) as the GUI, so requests made
 * here show up in the GUI history and the other way around.
 *
 * Built by CMake as the `cui_http_cli` target. This being a plain D program,
 * it must not call initDRuntime(): the runtime starts itself before main().
 *
 * History database: $CUI_HTTP_DB or ./requests.db by default. */
module cli;

import backend;
import core.thread : Thread;
import core.stdc.stdlib : free;
import core.time : msecs;
import std.algorithm.iteration : map;
import std.array : array, join;
import std.conv : to;
import std.getopt : config, GetOptException, getopt;
import std.process : environment;
import std.stdio;
import std.string : join, split, toStringz, toUpper;

const USAGE = q"EOS
Usage: cui-http-cli [OPTIONS] [METHOD] URL [BODY]

Options:
  -X, --method METHOD   HTTP method (default: GET)
  -d, --data BODY       request body (implies POST unless -X is given)
  -H, --header "K: V"   extra request header, repeatable
  --db PATH             history database file (default $CUI_HTTP_DB or ./requests.db)

Commands:
  history [COUNT]       show the last rows of the history (default 20)
  clear                 erase the whole history
  help                  show this text

Every request is saved to the same history the GUI reads.
EOS";

/* Send one request through the backend and print the response. */
int run_request(string method, string url, string body, const string[] headers,
                double timeout) {
    if (!url.length) {
        stderr.writeln("error: empty URL");
        return 2;
    }
    const headerBlock = headers.length ? headers.join("\n") : "";
    start_request_ex(cptr(method), cptr(url),
                     body.length ? cptr(body) : cast(const(char)*) null,
                     headerBlock.length ? cptr(headerBlock) : cast(const(char)*) null,
                     timeout > 0 ? cast(long) (timeout * 1000) : 0);
    int ticks = 0;
    while (!request_done()) {
        Thread.sleep(20.msecs);
        /* curl enforces the timeout; just keep the progress marker going */
        ticks++;
        if (ticks % 50 == 0) stderr.write('.'); 
    }
    if (ticks >= 50) stderr.writeln();

    auto res = last_response();
    switch (res.status_code) {
        case -1:
            stderr.writeln("error: network failure");
            break;
        case -2:
            stderr.writeln("cancelled");
            break;
        default:
            writefln("status: %s  (%.1f ms, %d bytes)",
                     res.status_code, res.latency_ms, res.body_len);
    }
    if (res.body_len && res.body !is null) {
        writeln(res.body[0 .. res.body_len]);
    }
    free_response(&res);
    return res.status_code >= 200 && res.status_code < 400 ? 0 : 1;
}

/* Print the last rows of the history, newest first. */
int print_history(size_t count) {
    import std.string : fromStringz;
    if (history_refresh() != 0) {
        stderr.writeln("error: cannot read the history");
        return 1;
    }
    auto n = cast(size_t) history_count();
    if (n > count) n = count;
    if (!n) {
        writeln("(history is empty)");
        return 0;
    }
    foreach (i; 0 .. n) {
        HistoryRow row;
        if (history_row(cast(int) i, &row) != 0) break;
        const when = fromStringz(row.created_at);
        writefln("#%-5d %4s %8.1fms %s  %-7s %s",
                 row.id,
                 row.status >= 0 ? row.status.to!string : "?",
                 row.latency_ms,
                 when.length >= 19 ? when[0 .. 19] : when,
                 fromStringz(row.method), fromStringz(row.url));
        free_string(row.method);
        free_string(row.url);
        free_string(row.created_at);
    }
    return 0;
}

private bool is_method(const string s) {
    foreach (m; ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"]) {
        if (s.toUpper == m) return true;
    }
    return false;
}

private string select_db(string explicitPath) {
    if (explicitPath.length) return explicitPath;
    return environment.get("CUI_HTTP_DB", "requests.db");
}

/* extern(C) parameters take const char*: cast off the immutability the D
 * strings carry. */
const(char)* cptr(string s) {
    return cast(const(char)*) toStringz(s);
}

int main(char[][] argv) {
    string[] args = argv.map!idup.array;
    string method;
    string dbPath;
    string body;
    string[] headers;
    double timeout;

    try {
        getopt(args,
            config.passThrough,
            "X|method", &method,
            "d|data", &body,
            "H|header", &headers,
            "db", &dbPath,
            "timeout", &timeout);
    } catch (GetOptException e) {
        stderr.writeln("error: ", e.msg);
        stderr.writeln(USAGE);
        return 2;
    }

    /* getopt keeps argv[0] and the positional arguments in args. */
    string[] rest = args[1 .. $];
    if (!rest.length) {
        stdout.writeln(USAGE);
        return 0;
    }

    /* Standalone commands. */
    const cmd = rest[0];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        stdout.writeln(USAGE);
        return 0;
    }
    if (cmd == "history") {
        const path = select_db(dbPath);
        if (init_db(cptr(path)) != 0) {
            stderr.writeln("error: cannot open the database ", path);
            return 1;
        }
        auto count = rest.length >= 2 ? rest[1].to!size_t : 20;
        return print_history(count);
    }
    if (cmd == "clear") {
        const path = select_db(dbPath);
        if (init_db(cptr(path)) != 0) {
            stderr.writeln("error: cannot open the database ", path);
            return 1;
        }
        clear_history();
        stdout.writeln("history cleared");
        return 0;
    }

    /* Request mode: [METHOD] URL [BODY]. */
    string[] positional = rest;
    string methodPos;
    if (positional.length && is_method(positional[0])) {
        methodPos = positional[0].toUpper;
        positional = positional[1 .. $];
    }
    if (!positional.length) {
        stderr.writeln("error: no URL given (try cui-http-cli help)");
        return 2;
    }
    string urlPos = positional[0];
    string bodyPos = positional.length > 1 ? positional[1 .. $].join(" ") : "";

    if (bodyPos.length && !body.length) body = bodyPos; /* -d wins otherwise */
    if (body.length && !method.length && !methodPos.length) method = "POST"; /* -d implies POST, as curl */
    if (!method.length) method = methodPos.length ? methodPos : "GET";

    const path = select_db(dbPath);
    if (init_db(cptr(path)) != 0) {
        stderr.writeln("error: cannot open the database ", path);
        return 1;
    }
    start_worker();
    scope (exit) backend_shutdown();

    return run_request(method, urlPos, body, headers, timeout);
}
