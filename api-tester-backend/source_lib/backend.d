module backend;

import core.runtime;
import core.memory;
import core.stdc.stdlib : malloc, free;
import core.stdc.string : memcpy;
import std.string : fromStringz, toStringz;
import std.datetime.stopwatch : StopWatch, AutoStart;
import std.net.curl : HTTP, get, post;
import d2sqlite3;
import std.conv;



extern (C) bool initDRuntime() {
        return Runtime.initialize();
    }

extern (C) bool terminateDRuntime() {
        return Runtime.terminate();
    }


extern (C) struct HttpResponse {
    int status_code;
    double latency_ms;
    char* body;
}

private __gshared Database db;
private __gshared bool dbInitialized = false;

extern (C) int init_db(const char* db_path) {
    try {
        db = Database(to!string(db_path));
        db.run("CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY AUTOINCREMENT, method TEXT, url TEXT, status INTEGER, latency_ms REAL);");
        dbInitialized = true;
        return 0;
    } catch (Exception e) {
        return -1;
    }
}

extern (C) HttpResponse execute_and_save(const char* c_method, const char* c_url, const char* c_body) {
    HttpResponse res = {0, 0.0, null};
    string url = cast(string) fromStringz(c_url);
    string method = cast(string) fromStringz(c_method);

    auto sw = StopWatch(AutoStart.yes);
    auto client = HTTP();
    string body_str;

    try {
        if (method == "GET") {
            body_str = cast(string) get(url, client);
        } else {
            string reqBody = c_body !is null ? cast(string) fromStringz(c_body) : "";
            ubyte[] content = cast(ubyte[]) reqBody;
            body_str = cast(string) post(url, content, client);
        }
        res.status_code = client.statusLine.code;
    } catch (Exception e) {
        res.status_code = -1;
        body_str = e.msg;
    }
    sw.stop();
    res.latency_ms = sw.peek.total!"nsecs" / 1_000_000.0;

    if (dbInitialized) {
        try {
            auto stmt = db.prepare("INSERT INTO history (method, url, status, latency_ms) VALUES (?, ?, ?, ?);");
            stmt.bind(1, method);
            stmt.bind(2, url);
            stmt.bind(3, res.status_code);
            stmt.bind(4, res.latency_ms);
            stmt.execute();
        } catch (Exception e) {
            // Log or ignore db error
        }
    }

    res.body = cast(char*) malloc(body_str.length + 1);
    if (res.body !is null) {
        memcpy(res.body, body_str.ptr, body_str.length);
        res.body[body_str.length] = '\0';
    }

    return res;
}

extern (C) void free_response(HttpResponse* res) {
    if (res !is null && res.body !is null) {
        free(res.body);
        res.body = null;
    }
}
