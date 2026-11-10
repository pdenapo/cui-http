/* cui-http: a native API client built on the CUI framework.
 *
 * Frontend: C99 + CUI components (request bar, params editor, JSON viewer,
 * tables). Backend: D, compiled to a static library by CMake — SQLite
 * persistence and libcurl networking (see api-tester-backend/bridge.h).
 *
 * Views: Request (send HTTP requests) and History (what has been sent).
 * English and Spanish UI text, light/dark theme; both settings persist in
 * the backend's SQLite database. */
#include <cui/cui.h>
#include <cui/cui_app.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "bridge.h"

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/* ------------------------------------------------------------------------ */

#define URL_CAP        1024
#define BODY_CAP       16384
#define RESP_CAP       131072
#define PARAMS_CAP     16
#define HIST_CAP       400

typedef struct {
    long id;
    char method[12];
    char url[512];
    char created[40];
    int status;
    double latency_ms;
} hist_item;

typedef struct {
    int nav;                    /* NAV_REQUEST / NAV_HISTORY */
    int lang;                   /* LANG_EN / LANG_ES */
    int method;                 /* cui_http_method */
    int tab;                    /* request tabs: body / params */
    int rtab;                   /* response tabs: preview / raw */
    char url[URL_CAP];
    char body[BODY_CAP];
    cui_kv_pair params[PARAMS_CAP];
    int nparams;

    bool loading;               /* a request is running in the backend */
    bool shown;                 /* the last response was fetched and shown */
    bool has_resp;
    int status_code;
    double latency_ms;
    long res_bytes;
    char resp_body[RESP_CAP];

    bool dark;
    bool palette;
    bool drawer;
    bool first_frame;

    hist_item hist[HIST_CAP];
    int nhist;
    bool hist_stale;
    char hist_filter[64];
    int hist_sel;
} app_state;

enum { NAV_REQUEST = 0, NAV_HISTORY };
enum { TAB_BODY = 0, TAB_PARAMS };
enum { RTAB_PREVIEW = 0, RTAB_RAW };
enum { LANG_EN = 0, LANG_ES, LANG_COUNT };

/* ------------------------------------------------------------------------ */
/* Translations (English and Spanish)                                        */
/* ------------------------------------------------------------------------ */

enum {
    T_REQUEST, T_HISTORY, T_ACTIONS, T_NAVIGATION, T_LANGUAGE, T_ENGLISH, T_SPANISH,
    T_BODY, T_PARAMS, T_RESPONSE, T_PREVIEW, T_RAW, T_URL_PH, T_BODY_PH,
    T_NO_RESPONSE, T_EMPTY, T_ERROR, T_CANCELLED, T_METHOD, T_URL, T_STATUS,
    T_TIME, T_WHEN, T_SEARCH_PH, T_REFRESH, T_CLEAR, T_NEWEST, T_ROWS,
    T_COPIED, T_SENT, T_CLEARED, T_SEND, T_EMPTY_HIST, T_COUNT
};

static const char *dict[LANG_COUNT][T_COUNT] = {
    { "Request", "History", "Actions", "Navigation", "Language", "English", "Spanish",
      "Body", "Params", "Response", "Preview", "Raw", "https://api.example.com/v1/users",
      "Request body, e.g. application/json",
      "Send a request to see the response here.", "(no body)", "Network error",
      "Cancelled", "Method", "URL", "Status", "Time", "When", "Filter…",
      "Refresh", "Clear history", "Newest first", "Showing %d requests",
      "Copied", "Request sent", "History cleared", "Send", "No requests yet" },
    { "Petición", "Historial", "Acciones", "Navegación", "Idioma", "English", "Español",
      "Cuerpo", "Parámetros", "Respuesta", "Vista", "Texto", "https://api.example.com/v1/users",
      "Cuerpo de la petición, p.ej. application/json",
      "Envía una petición para ver aquí la respuesta.", "(sin cuerpo)", "Error de red",
      "Cancelada", "Método", "URL", "Estado", "Tiempo", "Fecha", "Filtrar…",
      "Actualizar", "Borrar historial", "Más recientes primero", "Mostrando %d peticiones",
      "Copiado", "Petición enviada", "Historial borrado", "Enviar",
      "No hay peticiones aún" },
};

static const char *msg(app_state *s, int id) {
    return dict[s->lang][id];
}

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

static bool compact(cui_ctx *ui) { return cui_viewport_size(ui).x < 760; }

static cui_tone status_tone(int code) {
    if (code >= 200 && code < 300) return CUI_TONE_SUCCESS;
    if (code >= 300 && code < 400) return CUI_TONE_INFO;
    if (code >= 400 && code < 500) return CUI_TONE_WARNING;
    return CUI_TONE_DANGER;     /* 5xx, network errors and cancellations */
}

static const char *reason_text(app_state *s, char *buf, size_t cap) {
    (void)s;
    (void)buf;
    (void)cap;
    return "";
}

static bool looks_like_json(const char *b) {
    while (b && (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n')) b++;
    return b && (b[0] == '{' || b[0] == '[');
}

static bool contains_ignore_case(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncasecmp(p, needle, n) == 0) return true;
    }
    return false;
}

static int method_from_name(const char *name) {
    for (int m = 0; m <= CUI_HTTP_OPTIONS; m++) {
        if (strcmp(cui_http_method_name(m), name) == 0) return m;
    }
    return CUI_HTTP_GET;
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                   */
/* ------------------------------------------------------------------------ */

static void save_settings(app_state *s) {
    set_setting("language", s->lang == LANG_ES ? "es" : "en");
    set_setting("theme", s->dark ? "dark" : "light");
}

static void send_request(app_state *s) {
    if (s->loading || !s->url[0]) return;
    start_request(cui_http_method_name(s->method), s->url, s->body);
    s->loading = true;
    s->shown = false;
}

static void fetch_response(app_state *s) {
    HttpResponse res = last_response();
    s->status_code = res.status_code;
    s->latency_ms = res.latency_ms;
    s->res_bytes = res.body_len;
    s->has_resp = true;
    s->hist_stale = true;
    if (res.body_len > 0 && res.body_len < sizeof s->resp_body) {
        memcpy(s->resp_body, res.body, (size_t)res.body_len);
        s->resp_body[res.body_len] = '\0';
    } else if (res.body_len == 0) {
        s->resp_body[0] = '\0';
    } else {
        memcpy(s->resp_body, res.body, sizeof s->resp_body - 1);
        s->resp_body[sizeof s->resp_body - 1] = '\0';
    }
    free_response(&res);
    s->shown = true;
    s->loading = false;
}

static void reload_history(app_state *s) {
    s->nhist = 0;
    s->hist_sel = 0;
    if (history_refresh() == 0) {
        int n = history_count();
        if (n > HIST_CAP) n = HIST_CAP;
        for (int i = 0; i < n; i++) {
            HistoryRow r;
            if (history_row(i, &r) != 0) break;
            hist_item *h = &s->hist[i];
            h->id = r.id;
            snprintf(h->method, sizeof h->method, "%s", r.method ? r.method : "");
            snprintf(h->url, sizeof h->url, "%s", r.url ? r.url : "");
            snprintf(h->created, sizeof h->created, "%s", r.created_at ? r.created_at : "");
            h->method[sizeof h->method - 1] = '\0';
            h->url[sizeof h->url - 1] = '\0';
            h->created[sizeof h->created - 1] = '\0';
            h->status = r.status;
            h->latency_ms = r.latency_ms;
            free_string(r.method);
            free_string(r.url);
            free_string(r.created_at);
            s->nhist++;
        }
    }
    s->hist_stale = false;
}

/* Loads a history row back into the request bar. */
static void reuse_history(app_state *s, int i) {
    s->method = method_from_name(s->hist[i].method);
    snprintf(s->url, sizeof s->url, "%s", s->hist[i].url);
    s->nparams = cui_url_params(s->url, s->params, PARAMS_CAP);
    s->nav = NAV_REQUEST;
}

/* ------------------------------------------------------------------------ */
/* Views                                                                     */
/* ------------------------------------------------------------------------ */

static void nav_items(cui_ctx *ui, app_state *s) {
    static const char *items[] = { NULL, NULL };
    static const char *icons[] = { CUI_ICON_SEND, CUI_ICON_HISTORY };
    items[0] = msg(s, T_REQUEST);
    items[1] = msg(s, T_HISTORY);
    for (int i = 0; i < 2; i++) {
        if (cui_nav_item(ui, items[i], .icon = icons[i], .selected = s->nav == i)) {
            s->nav = i;
            s->drawer = false;
        }
    }
}

static void sidebar(cui_ctx *ui, app_state *s) {
    cui_column(ui, .width = CUI_PX(232), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE_MUTED, .pad = 12, .gap = 2) {
        cui_row(ui, .gap = 10, .pad_x = 8, .pad_top = 6, .pad_bottom = 16) {
            cui_box(ui, .width = CUI_PX(26), .height = CUI_PX(26), .radius = 7, .bg = CUI_COLOR_FOREGROUND, .dir = CUI_STACK,
                    .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_text(ui, "C", .font = CUI_FONT_DISPLAY, .size = 14, .color = CUI_COLOR_BACKGROUND);
            }
            cui_text(ui, "cui-http", .font = CUI_FONT_MEDIUM, .size = 15, .nowrap = true);
        }
        nav_items(ui, s);
        cui_spacer(ui);
    }
}

static void topbar(cui_ctx *ui, app_state *s) {
    bool narrow = compact(ui);
    cui_row(ui, .gap = 8, .pad_x = narrow ? 12 : 24, .pad_y = 12, .width = CUI_FILL) {
        if (narrow && cui_icon_button(ui, CUI_ICON_MENU, .tooltip = "Menu")) s->drawer = true;
        cui_text(ui, s->nav == NAV_REQUEST ? msg(s, T_REQUEST) : msg(s, T_HISTORY),
                 .font = CUI_FONT_MEDIUM, .size = 15, .nowrap = true);
        cui_spacer(ui);
        cui_menu(ui, msg(s, T_LANGUAGE), .icon = CUI_ICON_GLOBE, .variant = CUI_BUTTON_GHOST, .placement = CUI_PLACE_BOTTOM_END) {
            if (cui_menu_item(ui, "English", .icon = CUI_ICON_GLOBE, .checked = s->lang == LANG_EN)) {
                s->lang = LANG_EN;
                save_settings(s);
            }
            if (cui_menu_item(ui, "Español", .icon = CUI_ICON_GLOBE, .checked = s->lang == LANG_ES)) {
                s->lang = LANG_ES;
                save_settings(s);
            }
        }
        if (cui_icon_button(ui, cui_get_theme_mode(ui) == CUI_THEME_DARK ? CUI_ICON_SUN : CUI_ICON_MOON,
                            .tooltip = "Toggle theme")) {
            s->dark = cui_get_theme_mode(ui) != CUI_THEME_DARK;
            cui_set_theme(ui, s->dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
            save_settings(s);
        }
    }
}

/* The whole method + URL + params + body + response page. */
static void request_view(cui_ctx *ui, app_state *s) {
    bool narrow = compact(ui);

    cui_request_bar_result r = cui_request_bar(ui, "request", &s->method, s->url,
                                               sizeof s->url, .loading = s->loading);
    if (r.changed) s->nparams = cui_url_params(s->url, s->params, PARAMS_CAP);
    if (r.send) send_request(s);
    if (r.cancel) cancel_request();

    if (s->loading) {
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER, .width = CUI_FILL) {
            cui_spinner(ui, 16);
            cui_text(ui, s->url[0] ? s->url : "", .nowrap = true, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }
    }

    static const char *tabs[] = { NULL, NULL };
    tabs[0] = msg(s, T_BODY);
    tabs[1] = msg(s, T_PARAMS);
    cui_tabs(ui, "req_tabs", tabs, 2, &s->tab, .variant = CUI_TABS_UNDERLINE);

    if (s->tab == TAB_BODY) {
        cui_textarea(ui, "body", s->body, sizeof s->body,
                     .placeholder = msg(s, T_BODY_PH), .height = 160);
    } else {
        if (cui_kv_editor(ui, "params", s->params, &s->nparams, PARAMS_CAP)) {
            cui_url_set_params(s->url, sizeof s->url, s->params, s->nparams);
        }
    }

    /* ---- response ---- */
    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER, .width = CUI_FILL, .pad_top = 12) {
        cui_text(ui, msg(s, T_RESPONSE), .font = CUI_FONT_MEDIUM, .size = 14, .width = CUI_FILL, .nowrap = true);
        if (s->has_resp && cui_copy_button(ui, "copy_resp", s->resp_body)) {
            cui_toast(ui, msg(s, T_COPIED), .tone = CUI_TONE_SUCCESS);
        }
    }
    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER, .width = CUI_FILL) {
        if (s->has_resp) {
            if (s->status_code == -1) {
                cui_badge(ui, msg(s, T_ERROR), .tone = CUI_TONE_DANGER, .dot = true);
            } else if (s->status_code == -2) {
                cui_badge(ui, msg(s, T_CANCELLED), .tone = CUI_TONE_WARNING, .dot = true);
            } else {
                cui_response_status(ui, s->status_code, s->latency_ms / 1000.0, (double)s->res_bytes);
            }
        }
    }

    if (!s->has_resp) {
        cui_text(ui, msg(s, T_NO_RESPONSE), .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    } else if (s->status_code == -1 || s->status_code == -2) {
        /* the error message itself is the body */
        cui_code_block(ui, "resp_err", s->resp_body, .lang = "text", .max_height = 460);
    } else if (s->resp_body[0] == '\0') {
        cui_text(ui, msg(s, T_EMPTY), .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    } else {
        static const char *rtabs[] = { NULL, NULL };
        rtabs[0] = msg(s, T_PREVIEW);
        rtabs[1] = msg(s, T_RAW);
        cui_tabs(ui, "resp_tabs", rtabs, 2, &s->rtab, .variant = CUI_TABS_PILL);
        if (s->rtab == RTAB_PREVIEW && looks_like_json(s->resp_body)) {
            cui_json_view(ui, "resp_json", s->resp_body, .max_height = 460);
        } else {
            cui_code_block(ui, "resp_raw", s->resp_body,
                           .lang = looks_like_json(s->resp_body) ? "json" : "text", .max_height = 460);
        }
    }
}

/* The table of past requests, straight out of the backend's SQLite store. */
static void history_view(cui_ctx *ui, app_state *s) {
    bool narrow = compact(ui);
    cui_row(ui, .gap = 8, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_box(ui, .width = narrow ? CUI_PX(140) : CUI_PX(220), .min_width = 140) {
            cui_search(ui, "hist_filter", s->hist_filter, sizeof s->hist_filter,
                       .placeholder = msg(s, T_SEARCH_PH));
        }
        cui_spacer(ui);
        if (cui_button(ui, msg(s, T_CLEAR), .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .variant = CUI_BUTTON_GHOST)) {
            clear_history();
            s->hist_stale = true;
            cui_toast(ui, msg(s, T_CLEARED), .tone = CUI_TONE_WARNING);
        }
        if (cui_icon_button(ui, CUI_ICON_REFRESH_CW, .tooltip = msg(s, T_REFRESH))) s->hist_stale = true;
    }
    cui_text(ui, msg(s, T_NEWEST), .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

    cui_table_column cols[] = {
        { msg(s, T_METHOD), CUI_PX(90), CUI_TEXT_LEFT, false },
        { msg(s, T_URL), CUI_FILL, CUI_TEXT_LEFT, false },
        { msg(s, T_STATUS), CUI_PX(84), CUI_TEXT_LEFT, false },
        { msg(s, T_TIME), CUI_PX(84), CUI_TEXT_RIGHT, false },
        { msg(s, T_WHEN), CUI_PX(160), CUI_TEXT_LEFT, false },
    };
    int shown = 0;
    cui_table(ui, "history", cols, 5, .selectable = true) {
        for (int i = 0; i < s->nhist; i++) {
            hist_item *h = &s->hist[i];
            if (!contains_ignore_case(h->url, s->hist_filter) &&
                !contains_ignore_case(h->method, s->hist_filter)) continue;
            if (cui_table_row(ui, s->hist_sel == i)) {
                s->hist_sel = i;
                reuse_history(s, i);
            }
            shown++;
            cui_table_cell(ui);
            if (strcmp(h->method, "GET") == 0) cui_badge(ui, h->method, .tone = CUI_TONE_INFO);
            else if (strcmp(h->method, "POST") == 0) cui_badge(ui, h->method, .tone = CUI_TONE_SUCCESS);
            else if (strcmp(h->method, "DELETE") == 0 || strcmp(h->method, "PATCH") == 0) {
                cui_badge(ui, h->method, .tone = CUI_TONE_DANGER);
            } else {
                cui_badge(ui, h->method, .tone = CUI_TONE_NEUTRAL);
            }
            cui_table_cell_text(ui, h->url);
            cui_table_cell(ui);
            if (h->status >= 0) {
                char st[12];
                snprintf(st, sizeof st, "%d", h->status);
                cui_badge(ui, st, .tone = status_tone(h->status), .dot = true);
            } else {
                cui_badge(ui, h->status == -2 ? msg(s, T_CANCELLED) : msg(s, T_ERROR),
                          .tone = CUI_TONE_DANGER, .dot = true);
            }
            cui_table_cell_textf(ui, "%.0f ms", h->latency_ms);
            cui_table_cell_text(ui, h->created);
        }
    }
    if (shown == 0) {
        cui_text(ui, msg(s, T_EMPTY_HIST), .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    } else {
        char footer[64];
        snprintf(footer, sizeof footer, msg(s, T_ROWS), shown);
        cui_text(ui, footer, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
    }
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                     */
/* ------------------------------------------------------------------------ */

static void frame(cui_ctx *ui, void *user) {
    app_state *s = user;
    bool narrow = compact(ui);
    if (!narrow) s->drawer = false;

    if (s->first_frame) {
        s->first_frame = false;
        cui_set_theme(ui, s->dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
        s->nparams = cui_url_params(s->url, s->params, PARAMS_CAP);
    }

    /* Keyboard: Ctrl+K opens the palette, Ctrl+Enter sends the request. */
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_K)) s->palette = true;
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_ENTER)) {
        if (s->nav != NAV_REQUEST) s->nav = NAV_REQUEST;
        send_request(s);
    }

    /* Async bookkeeping with the backend. */
    if (s->loading && !s->shown && request_done()) fetch_response(s);
    if (s->nav == NAV_HISTORY && s->hist_stale) reload_history(s);

    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_STRETCH, .bg = CUI_COLOR_BACKGROUND) {
        if (!narrow) {
            sidebar(ui, s);
            cui_separator(ui);
        }
        cui_column(ui, .width = CUI_FILL) {
            topbar(ui, s);
            cui_separator(ui);
            cui_scroll(ui, .id = "content", .pad = narrow ? 16 : 32, .gap = narrow ? 16 : 24, .fade_edges = true) {
                if (s->nav == NAV_REQUEST) request_view(ui, s);
                else history_view(ui, s);
            }
        }
    }

    cui_sheet(ui, "cui-http", &s->drawer, .side = CUI_SIDE_LEFT, .size = 280) {
        nav_items(ui, s);
    }

    /* Command palette */
    cui_command cmds[] = {
        { msg(s, T_SEND), CUI_ICON_SEND, msg(s, T_ACTIONS), "Ctrl Enter" },
        { msg(s, T_HISTORY), CUI_ICON_HISTORY, msg(s, T_NAVIGATION), NULL },
        { msg(s, T_REFRESH), CUI_ICON_REFRESH_CW, msg(s, T_NAVIGATION), NULL },
        { msg(s, T_CLEAR), CUI_ICON_TRASH_2, msg(s, T_NAVIGATION), NULL },
    };
    int c = cui_command_palette(ui, &s->palette, cmds, 4);
    if (c == 0) {
        s->nav = NAV_REQUEST;
        send_request(s);
        s->palette = false;
    } else if (c == 1) {
        s->nav = NAV_HISTORY;
        s->palette = false;
    } else if (c == 2) {
        reload_history(s);
        s->palette = false;
    } else if (c == 3) {
        clear_history();
        s->hist_stale = true;
        cui_toast(ui, msg(s, T_CLEARED), .tone = CUI_TONE_WARNING);
        s->palette = false;
    }
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                               */
/* ------------------------------------------------------------------------ */

static void load_settings(app_state *s) {
    char *v = get_setting("language");
    if (v) {
        s->lang = strcmp(v, "es") == 0 ? LANG_ES : LANG_EN;
        free_string(v);
    }
    v = get_setting("theme");
    if (v) {
        s->dark = strcmp(v, "dark") == 0;
        free_string(v);
    }
}

int main(void) {
    if (!initDRuntime()) {
        fprintf(stderr, "failed to start the D runtime\n");
        return 1;
    }
    if (init_db("requests.db") != 0) {
        fprintf(stderr, "failed to initialize the SQLite database\n");
        backend_shutdown();
        terminateDRuntime();
        return 1;
    }
    start_worker();

    static app_state s = { .nparams = -1, .method = CUI_HTTP_GET };
    snprintf(s.body, sizeof s.body, "{\n  \"id\": 42\n}\n");
    load_settings(&s);

    cui_app *app = cui_app_create(&(cui_app_desc){
        .title = "cui-http",
        .width = 1360,
        .height = 900,
        .accent = CUI_ACCENT_BLUE,
    });
    if (!app) {
        backend_shutdown();
        terminateDRuntime();
        return 1;
    }

    cui_app_maximize(app);

    while (cui_app_frame(app)) {
        frame(cui_app_ui(app), &s);
    }

    cui_app_destroy(app);
    backend_shutdown();
    terminateDRuntime();
    return 0;
}
