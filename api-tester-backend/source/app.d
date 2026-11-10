/* Standalone D test program for the backend library.
 * Built by CMake (api_tester_d_test target); do not build it with dub. */
module app;

import backend;
import core.thread : Thread;
import core.time : msecs;
import std.stdio;

void main() {
    if (init_db("test.db") != 0) {
        writeln("failed to open the database");
        return;
    }
    start_worker();
    scope (exit) backend_shutdown();

    start_request("GET", "https://httpbin.org/get", "");
    while (!request_done()) Thread.sleep(50.msecs);

    auto res = last_response();
    switch (res.status_code) {
        case -1: writeln("network error"); break;
        case -2: writeln("cancelled"); break;
        default: writeln("status: ", res.status_code);
    }
    writeln("latency: ", res.latency_ms, " ms");
    writeln("body length: ", res.body_len);

    history_refresh();
    writeln("history rows: ", history_count());
    free_response(&res);
}
