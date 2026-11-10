/* Standalone D test program for the backend library.
 * Built by CMake (api_tester_d_test target); do not build it with dub.
 *
 * Exercises persistence logic (settings round trip, empty history, delete)
 * plus a live request when the network is available. */
module app;

import backend;
import core.thread : Thread;
import core.time : msecs;
import std.conv : to;
import std.stdio;
import std.string : toStringz, fromStringz;

void check(string name, bool cond) {
    if (cond) writeln("  [PASS] ", name);
    else writeln("  [FAIL] ", name);
}

void main() {
    string db = "test.db";
    if (init_db(cast(const(char)*) toStringz(db)) != 0) {
        writeln("init_db failed");
        return;
    }

    start_worker();
    scope (exit) backend_shutdown();

    /* ---- persistence: settings round trip ---- */
    set_setting(cast(const(char)*) toStringz("language"), cast(const(char)*) toStringz("es"));
    set_setting(cast(const(char)*) toStringz("theme"), cast(const(char)*) toStringz("dark"));
    auto lang = get_setting(cast(const(char)*) toStringz("language"));
    auto theme = get_setting(cast(const(char)*) toStringz("theme"));
    check("settings: language persisted", lang !is null && to!string(lang) == "es");
    check("settings: theme persisted", theme !is null && to!string(theme) == "dark");
    free_string(lang);
    free_string(theme);

    /* ---- persistence: empty history ---- */
    history_refresh();
    check("history: empty db has 0 rows", history_count() == 0);

    /* ---- persistence: delete on empty is a no-op ---- */
    delete_history_row(99999);
    history_refresh();
    check("history: delete missing row keeps 0 rows", history_count() == 0);

    /* ---- live request (best effort) ---- */
    start_request("GET", "https://httpbin.org/get", "");
    int ticks = 0;
    while (!request_done() && ticks < 200) { Thread.sleep(50.msecs); ticks++; }
    if (request_done()) {
        auto res = last_response();
        writeln("live request status: ", res.status_code, " (", res.latency_ms, " ms)");
        free_response(&res);

        history_refresh();
        check("history: request was persisted", history_count() >= 1);
        if (history_count() > 0) {
            HistoryRow row;
            if (history_row(0, &row) == 0) {
                writeln("  row: ", fromStringz(row.method), " ", row.status, " ", fromStringz(row.url));
                delete_history_row(row.id);
                free_string(row.method);
                free_string(row.url);
                free_string(row.created_at);
                free_string(row.req_headers);
                free_string(row.res_headers);
            }
            history_refresh();
            check("history: delete single row works", history_count() == 0);
        }
    } else {
        writeln("live request: network unavailable, skipping");
    }

    /* default db path helper */
    auto p = default_db_path();
    writeln("default db path: ", fromStringz(p));
    free_string(p);
}
