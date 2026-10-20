/* nocto_plugin.h - Nocto plugin C ABI (version 1).
 *
 * A plugin is a DLL exporting:
 *     NOCTO_EXPORT uint32_t nocto_plugin_abi(void);                                   // return NOCTO_PLUGIN_ABI
 *     NOCTO_EXPORT int      nocto_plugin_load(const nocto_host* host, nocto_plugin_info* info);   // 1 = ok
 *     NOCTO_EXPORT void     nocto_plugin_unload(void);                                // optional
 * nocto_plugin_load fills `info` and registers features through `host`. Descriptors (and every string / array they point
 * to) must stay valid until unload - use static storage. Nocto copies nothing it does not need.
 *
 * DLLs are loaded from %LOCALAPPDATA%\Nocto\plugins and from the "plugins" folder next to Nocto.exe.
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

#define NOCTO_PLUGIN_ABI 1u
#if defined(_WIN32)
#define NOCTO_EXPORT __declspec(dllexport)
#else
#define NOCTO_EXPORT
#endif

typedef enum nocto_prop_type { NOCTO_PROP_FLOAT = 0, NOCTO_PROP_INT = 1, NOCTO_PROP_BOOL = 2, NOCTO_PROP_COLOR = 3 } nocto_prop_type;
typedef struct nocto_prop {
    const char* id;          /* stable key */
    const char* label;       /* shown in the UI */
    int32_t type;            /* nocto_prop_type */
    float def, min, max;
} nocto_prop;

typedef enum nocto_log_level { NOCTO_LOG_INFO = 0, NOCTO_LOG_WARN = 1, NOCTO_LOG_ERROR = 2 } nocto_log_level;

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
       straight alpha) owned by the plugin and valid until the next call; set *w/*h. NULL = draw nothing. */
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
} nocto_host;

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