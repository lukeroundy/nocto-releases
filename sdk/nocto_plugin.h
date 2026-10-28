/* nocto_plugin.h - Nocto plugin C ABI (version 2; ABI 1 plugins keep loading unchanged).
 *
 * A plugin is a DLL exporting:
 *     NOCTO_EXPORT uint32_t nocto_plugin_abi(void);                                   // return NOCTO_PLUGIN_ABI
 *     NOCTO_EXPORT int      nocto_plugin_load(const nocto_host* host, nocto_plugin_info* info);   // 1 = ok
 *     NOCTO_EXPORT void     nocto_plugin_unload(void);                                // optional
 * nocto_plugin_load fills `info` and registers features through `host`. Descriptors (and every string / array they point
 * to) must stay valid until unload - use static storage. Nocto copies nothing it does not need.
 *
 * DLLs are loaded from %LOCALAPPDATA%\Nocto\plugins and from the "plugins" folder next to Nocto.exe.
 * Linux: the plugin is a shared library (.so) loaded with dlopen from ~/.local/share/Nocto/plugins (or $NOCTO_CONFIG_DIR/plugins)
 * and <dir of the nocto binary>/plugins. Same ABI; only the graphics handles differ (see "Linux graphics handles" below).
 * Shader-only video filters need no DLL: a <name>.json manifest + .hlsl file (see docs/PLUGINS.md).
 *
 * Threads: sources, video filters and outputs' video callback run on the render thread with the D3D11 immediate
 * context locked (do not lock it yourself, do not block). Audio filters run on the mixer thread (10 ms chunks).
 * Outputs' audio callback runs on the audio thread. Hotkey callbacks run on the UI thread.
 * Crashes (access violations etc.) inside any callback are caught: the plugin is disabled and the error shown in
 * the Plugins window; Nocto keeps running. Do not rely on this for normal error handling.
 *
 * Properties: every feature describes its settings as an array of nocto_prop; Nocto draws the settings UI and passes
 * the current values as a float array in the same order (bool 0/1, int, colour 0xRRGGBB as float).
 */
#ifndef NOCTO_PLUGIN_H
#define NOCTO_PLUGIN_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NOCTO_PLUGIN_ABI: what a plugin built against this header returns from nocto_plugin_abi(). Nocto loads plugins reporting 1 or 2.
 * ABI 2 adds host->get_api() (UI panels, menus, storage, secrets, HTTP / WebSocket - see "ABI 2" below). A plugin that only uses ABI-1
 * features can keep returning 1 (NOCTO_PLUGIN_ABI_V1). Always check host->abi >= 2 before calling host->get_api. */
#define NOCTO_PLUGIN_ABI_V1 1u
#define NOCTO_PLUGIN_ABI 2u
#if defined(_WIN32)
#define NOCTO_EXPORT __declspec(dllexport)
#else
#define NOCTO_EXPORT __attribute__((visibility("default")))
#endif

typedef enum nocto_prop_type { NOCTO_PROP_FLOAT = 0, NOCTO_PROP_INT = 1, NOCTO_PROP_BOOL = 2, NOCTO_PROP_COLOR = 3 } nocto_prop_type;
typedef struct nocto_prop {
    const char* id;          /* stable key */
    const char* label;       /* shown in the UI */
    int32_t type;            /* nocto_prop_type */
    float def, min, max;
} nocto_prop;

typedef enum nocto_log_level { NOCTO_LOG_INFO = 0, NOCTO_LOG_WARN = 1, NOCTO_LOG_ERROR = 2 } nocto_log_level;

/* Linux graphics handles. There is no D3D11: every `device` / `context` argument is NULL and an OpenGL 3.3 core context is current on the
 * calling thread (render thread for sources / video filters / output video). The host saves and restores its GL state around each call.
 *   - "ID3D11ShaderResourceView*" = GL_TEXTURE_2D name stored in the pointer: (void*)(uintptr_t)name. Source textures are RGBA8, straight
 *     alpha, row 0 of the uploaded data = top of the picture.
 *   - "ID3D11RenderTargetView*" (video filter out) = framebuffer object name, already bound with the viewport set to w x h; draw into it.
 *   - Video output gets the canvas texture name (RGBA8, row 0 = top).
 * Video filters are called with in = texture name and out = FBO name; a filter must not keep GL objects bound. Link plugins with -lGL.
 * A crash (SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT) inside a callback is caught like on Windows; do not rely on it. */

/* ---- source: produces a texture every frame ---- */
typedef struct nocto_source_desc {
    const char* id;          /* unique within the plugin */
    const char* name;        /* "Add source" menu label */
    const nocto_prop* props; uint32_t prop_count;
    /* device: ID3D11Device*. Return an instance pointer (NULL = failure). */
    void* (*create)(void* device, const float* values);
    void (*destroy)(void* inst);
    void (*update)(void* inst, const float* values);                 /* settings changed (optional) */
    /* context: ID3D11DeviceContext*. Return ID3D11ShaderResourceView* of a BGRA8/RGBA8 texture (sRGB-encoded,
       straight alpha) owned by the plugin and valid until the next call; set *w and *h. NULL = draw nothing. */
    void* (*render)(void* inst, void* context, double time_sec, uint32_t* w, uint32_t* h);
} nocto_source_desc;

/* ---- video filter: in texture -> out render target (same size / format) ---- */
typedef struct nocto_video_filter_desc {
    const char* id; const char* name;
    const nocto_prop* props; uint32_t prop_count;
    void* (*create)(void* device);
    void (*destroy)(void* inst);
    /* in_srv: ID3D11ShaderResourceView*, out_rtv: ID3D11RenderTargetView* (bound state is not preserved by Nocto;
       set everything you use). Return 1 if out was written, 0 to pass the input through unchanged. */
    int (*process)(void* inst, void* context, void* in_srv, void* out_rtv, uint32_t w, uint32_t h, const float* values, double time_sec);
} nocto_video_filter_desc;

/* ---- audio filter: in-place, interleaved float ---- */
typedef struct nocto_audio_filter_desc {
    const char* id; const char* name;
    const nocto_prop* props; uint32_t prop_count;
    void* (*create)(uint32_t sample_rate, uint32_t channels);
    void (*destroy)(void* inst);
    void (*process)(void* inst, float* samples, uint32_t frames, const float* values);
} nocto_audio_filter_desc;

/* ---- output: receives the program (canvas) frames + mixed audio while started from the Plugins window ---- */
typedef struct nocto_output_desc {
    const char* id; const char* name;
    const nocto_prop* props; uint32_t prop_count;
    void* (*start)(void* device, const float* values);
    void (*stop)(void* inst);
    /* canvas_srv: ID3D11ShaderResourceView* (BGRA8 sRGB, or FP16 scRGB on HDR canvases), pts in 100 ns QPC units */
    void (*video)(void* inst, void* context, void* canvas_srv, uint32_t w, uint32_t h, int64_t pts);
    void (*audio)(void* inst, const float* samples, uint32_t frames, uint32_t sample_rate, uint32_t channels, int64_t pts);
} nocto_output_desc;

/* ---- hotkey action: listed in Settings > Hotkeys ---- */
typedef struct nocto_hotkey_desc {
    const char* id; const char* label;
    void (*pressed)(void* user);
    void* user;
} nocto_hotkey_desc;

/* ---- scripting hook (reserved): a future script host registers here. Nocto 1.x returns 0 (unsupported). ---- */
typedef struct nocto_script_engine_desc {
    const char* id; const char* name; const char* file_extension;
    int (*run_file)(void* user, const char* utf8_path);
    void* user;
} nocto_script_engine_desc;

typedef struct nocto_host {
    uint32_t abi;            /* NOCTO_PLUGIN_ABI of the host */
    const char* host_version;
    void* plugin;            /* opaque handle, pass back to every call */
    void (*log)(void* plugin, int32_t level, const char* utf8_message);
    int (*register_source)(void* plugin, const nocto_source_desc* d);
    int (*register_video_filter)(void* plugin, const nocto_video_filter_desc* d);
    int (*register_audio_filter)(void* plugin, const nocto_audio_filter_desc* d);
    int (*register_output)(void* plugin, const nocto_output_desc* d);
    int (*register_hotkey)(void* plugin, const nocto_hotkey_desc* d);
    int (*register_script_engine)(void* plugin, const nocto_script_engine_desc* d);
    /* ---- ABI 2 (valid only when host->abi >= 2; the struct is only ever extended at the end) ----
     * Returns a pointer to a const API table, or NULL when the host does not know that name / version:
     *   "nocto.ui"      version 1 -> const nocto_ui_v1*        (widgets for panels)
     *   "nocto.app"     version 1 -> const nocto_app_v1*       (panels, menus, UI-thread tasks, stream state, open_url, textures)
     *   "nocto.storage" version 1 -> const nocto_storage_v1*   (per-plugin settings JSON + secret storage)
     *   "nocto.net"     version 1 -> const nocto_net_v1*       (async HTTP + WebSocket)
     * Every table starts with `uint32_t size` (sizeof the table the host filled in): fields beyond `size` do not exist. */
    const void* (*get_api)(void* plugin, const char* name, uint32_t version);
} nocto_host;


/* =====================================================================================================================
 * ABI 2
 * ---------------------------------------------------------------------------------------------------------------------
 * Threads: panel draw callbacks, menu callbacks, post_to_ui tasks, HTTP completions and WebSocket events ALL run on the UI thread
 * (inside Nocto's frame, one after another) - a plugin does not need locks for state touched only from those callbacks. Do not block
 * in them. The calls of nocto_ui_v1 are only valid inside a panel draw callback; nocto_app_v1.post_to_ui is the only call that
 * may be made from any thread. Every callback is crash-guarded like the ABI-1 callbacks (a crash disables the plugin).
 * Sizes passed to nocto_ui_v1 (widths, heights, spacing) are in "dp": 100%-scale pixels. Nocto multiplies them by the UI scale (up to
 * 300%), so never pre-scale. 0 = automatic size, negative width = stretch to the right edge leaving -width dp.
 * Colours are 0xAABBGGRR packed into a uint32_t (NOCTO_RGBA).
 * ================================================================================================================== */
#define NOCTO_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

enum { NOCTO_INPUT_ENTER_RETURNS = 1, NOCTO_INPUT_PASSWORD = 2, NOCTO_INPUT_READONLY = 4, NOCTO_INPUT_AUTOSELECT = 8 };

typedef struct nocto_ui_v1 {
    uint32_t size;
    /* text */
    void (*text)(const char* utf8);
    void (*text_wrapped)(const char* utf8);
    void (*text_disabled)(const char* utf8);
    void (*text_colored)(uint32_t rgba, const char* utf8);
    void (*separator_text)(const char* utf8);          /* a section heading with a rule */
    void (*separator)(void);
    void (*spacing)(void);
    /* widgets: return 1 when the value was changed / the button pressed */
    int (*button)(const char* label, float w_dp, float h_dp);
    int (*small_button)(const char* label);
    int (*checkbox)(const char* label, int* value);
    int (*input_text)(const char* label, char* buf, uint32_t cap, uint32_t flags, const char* hint);
    int (*input_multiline)(const char* label, char* buf, uint32_t cap, uint32_t flags, float w_dp, float h_dp);
    int (*input_int)(const char* label, int* value);
    int (*input_float)(const char* label, float* value);
    int (*slider_float)(const char* label, float* value, float lo, float hi);
    int (*slider_int)(const char* label, int* value, int lo, int hi);
    /* combo: items = "A\0B\0C\0\0" (zero separated, double-zero terminated) */
    int (*combo)(const char* label, int* current, const char* items);
    int (*begin_combo)(const char* label, const char* preview);   /* if it returns 1 call end_combo() */
    void (*end_combo)(void);
    int (*selectable)(const char* label, int selected);
    /* layout */
    void (*same_line)(float offset_dp, float spacing_dp);          /* both 0 = default */
    void (*new_line)(void);
    void (*set_next_item_width)(float w_dp);
    float (*content_width)(void);                                  /* remaining width in dp */
    float (*content_height)(void);
    int (*begin_child)(const char* id, float w_dp, float h_dp, int border);   /* always call end_child() afterwards */
    void (*end_child)(void);
    float (*scroll_y)(void);
    float (*scroll_max_y)(void);
    void (*set_scroll_here_y)(float ratio);                        /* 1 = scroll to the bottom */
    void (*push_id_str)(const char* id);
    void (*push_id_int)(int id);
    void (*pop_id)(void);
    void (*push_text_color)(uint32_t rgba);
    void (*pop_text_color)(void);
    int (*begin_tab_bar)(const char* id);                          /* if it returns 1 call end_tab_bar() */
    void (*end_tab_bar)(void);
    int (*begin_tab_item)(const char* label);                      /* if it returns 1 call end_tab_item() */
    void (*end_tab_item)(void);
    void (*begin_disabled)(int disabled);                          /* always paired with end_disabled() */
    void (*end_disabled)(void);
    void (*progress_bar)(float fraction, float w_dp, float h_dp, const char* overlay);
    void (*tooltip)(const char* utf8);                             /* shown when the previous item is hovered */
    int (*is_item_hovered)(void);
    float (*text_width)(const char* utf8);                         /* in dp */
    void (*image)(uint64_t texture, float w_dp, float h_dp);       /* texture from nocto_app_v1.texture_create */
    void (*set_clipboard)(const char* utf8);
} nocto_ui_v1;

/* ---- panels (dock windows) and menu items ---- */
typedef enum nocto_dock_hint {
    NOCTO_DOCK_FLOAT = 0,    /* floating window the first time; afterwards wherever the user left it */
    NOCTO_DOCK_RIGHT = 1,    /* a new column on the right of the layout (all RIGHT panels of all plugins share it as tabs) */
    NOCTO_DOCK_BOTTOM = 2,   /* tabbed next to Filters / Audio Mixer */
    NOCTO_DOCK_LEFT = 3
} nocto_dock_hint;
typedef struct nocto_panel_desc {
    const char* id;          /* unique within the plugin */
    const char* title;       /* window title / View menu entry (keep it unique across plugins) */
    int32_t dock;            /* nocto_dock_hint, first run only: Nocto persists the user's layout and open/closed state */
    int32_t default_open;    /* 1 = open the first time the plugin is seen */
    float default_w, default_h;   /* dp, used for the floating case */
    void (*draw)(void* user, const nocto_ui_v1* ui);
    void* user;
} nocto_panel_desc;
enum { NOCTO_MENU_DISABLED = 1, NOCTO_MENU_CHECKED = 2 };
typedef struct nocto_menu_item_desc {
    const char* menu;        /* top-level menu title, e.g. "Twitch"; NULL / "" = the shared "Plugins" menu; "View" = the View menu */
    const char* label;       /* "-" = separator */
    void (*clicked)(void* user);
    uint32_t (*state)(void* user);   /* optional: NOCTO_MENU_* bits queried every frame */
    void* user;
} nocto_menu_item_desc;

typedef struct nocto_stream_state {
    uint32_t size;           /* in: sizeof(nocto_stream_state); the host fills only fields that fit */
    int32_t streaming;       /* 1 while any stream destination is live */
    int32_t recording;
    int32_t twitch_destination;   /* 1 when one of the stream destinations is the Twitch service */
    double ui_scale;         /* 1.0 = 100% */
} nocto_stream_state;

typedef struct nocto_app_v1 {
    uint32_t size;
    int (*register_panel)(void* plugin, const nocto_panel_desc* d);
    int (*register_menu_item)(void* plugin, const nocto_menu_item_desc* d);
    void (*set_panel_open)(void* plugin, const char* panel_id, int open);   /* UI thread */
    int (*is_panel_open)(void* plugin, const char* panel_id);
    /* any thread: runs fn(user) later on the UI thread (drops the task if the plugin has been disabled) */
    void (*post_to_ui)(void* plugin, void (*fn)(void* user), void* user);
    int (*open_url)(void* plugin, const char* https_url);                   /* default browser; only http(s) URLs */
    int (*get_stream_state)(void* plugin, nocto_stream_state* out);         /* any thread */
    void (*toast)(void* plugin, const char* utf8);                          /* short notice at the bottom of the window */
    int64_t (*now_ms)(void);                                                /* monotonic */
    /* UI thread only. RGBA8 straight alpha, row 0 = top. Returns 0 on failure. Textures are freed by texture_destroy (or at exit). */
    uint64_t (*texture_create)(void* plugin, const uint8_t* rgba, uint32_t w, uint32_t h);
    void (*texture_destroy)(void* plugin, uint64_t texture);
    /* any thread: like post_to_ui but runs fn(user) on the UI thread after at least delay_ms (a timer, checked once per frame).
     * Returns an id for cancel_task (0 = not scheduled). Added in SDK 1.1: check `size >= sizeof(nocto_app_v1)` before using. */
    uint64_t (*post_to_ui_after)(void* plugin, uint32_t delay_ms, void (*fn)(void* user), void* user);
    void (*cancel_task)(void* plugin, uint64_t id);                         /* any thread; harmless when the task already ran */
} nocto_app_v1;

/* ---- storage ---- */
typedef struct nocto_storage_v1 {
    uint32_t size;
    /* Per-plugin settings document (any JSON text you like, up to 1 MiB), kept in Nocto's data folder. Not part of scene
     * exports / profiles. settings_load copies up to cap-1 bytes + NUL into buf and returns the full length (0 = none yet). */
    int32_t (*settings_load)(void* plugin, char* buf, uint32_t cap);
    int (*settings_save)(void* plugin, const char* utf8_json);
    /* Secrets (tokens, keys): Windows Credential Manager; Linux Secret Service (libsecret) with a 0600 file fallback.
     * Per plugin and key (key: 1-64 chars of [A-Za-z0-9_.-], value up to 2500 bytes), never exported in profiles and never written to a log. secret_get returns the value length (-1 = none). */
    int (*secret_set)(void* plugin, const char* key, const char* value);
    int32_t (*secret_get)(void* plugin, const char* key, char* buf, uint32_t cap);
    int (*secret_delete)(void* plugin, const char* key);
} nocto_storage_v1;

/* ---- network (TLS included). All completions are delivered on the UI thread. ---- */
typedef struct nocto_http_request {
    const char* method;      /* "GET", "POST", "PATCH", ... (NULL = GET) */
    const char* url;         /* http:// or https:// */
    const char* headers;     /* "Name: value\nName2: value2" (NULL = none) */
    const void* body; uint32_t body_len;
    uint32_t timeout_ms;     /* 0 = 20 s */
} nocto_http_request;
typedef struct nocto_http_response {
    int32_t status;          /* HTTP status; 0 = transport error (see error) */
    const char* error;       /* NULL when status != 0 */
    const char* headers;     /* "name: value\n..." (names lower-cased) */
    const char* body; uint32_t body_len;   /* NUL-terminated as well; valid only during the callback */
} nocto_http_response;
typedef void (*nocto_http_cb)(void* user, const nocto_http_response* r);
typedef struct nocto_ws_events {
    void (*on_open)(void* user);
    void (*on_message)(void* user, const char* data, uint32_t len, int is_text);
    void (*on_close)(void* user, int32_t code, const char* reason);   /* always delivered exactly once after ws_open succeeded (also on failure to connect: code 1006) */
    void* user;
} nocto_ws_events;
typedef struct nocto_net_v1 {
    uint32_t size;
    uint64_t (*http_request)(void* plugin, const nocto_http_request* req, nocto_http_cb cb, void* user);   /* 0 = could not start */
    void (*http_cancel)(void* plugin, uint64_t id);               /* the callback is not delivered afterwards */
    uint64_t (*ws_open)(void* plugin, const char* url, const char* headers, const nocto_ws_events* ev);   /* ws:// or wss://; 0 = could not start */
    int (*ws_send)(void* plugin, uint64_t id, const void* data, uint32_t len, int is_text);                /* 1 = queued */
    void (*ws_close)(void* plugin, uint64_t id);
} nocto_net_v1;

typedef struct nocto_plugin_info {
    uint32_t abi;            /* set to NOCTO_PLUGIN_ABI */
    const char* name;
    const char* version;
    const char* author;
    const char* description;
} nocto_plugin_info;

typedef uint32_t (*nocto_plugin_abi_fn)(void);
typedef int (*nocto_plugin_load_fn)(const nocto_host* host, nocto_plugin_info* info);
typedef void (*nocto_plugin_unload_fn)(void);

#ifdef __cplusplus
}
#endif
#endif /* NOCTO_PLUGIN_H */