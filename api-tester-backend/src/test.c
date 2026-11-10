/* test.c - standalone test program for the C backend (persistence logic). */
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const char *name, int cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
}

int main(void) {
    const char *db = "test.db";
    remove(db);
    if (backend_init(db) != 0) { fprintf(stderr, "init_db failed\n"); return 1; }

    /* settings round trip */
    set_setting("language", "it");
    set_setting("theme", "dark");
    char *lang = get_setting("language");
    char *theme = get_setting("theme");
    check("settings: language persisted", lang && strcmp(lang, "it") == 0);
    check("settings: theme persisted", theme && strcmp(theme, "dark") == 0);
    free(lang); free(theme);

    /* empty history */
    history_refresh();
    check("history: empty db has 0 rows", history_count() == 0);

    /* delete missing row is a no-op */
    delete_history_row(99999);
    history_refresh();
    check("history: delete missing keeps 0 rows", history_count() == 0);

    /* default db path */
    char *p = default_db_path();
    printf("default db path: %s\n", p ? p : "(null)");
    free(p);

    backend_shutdown();
    remove(db);
    printf("done\n");
    return 0;
}