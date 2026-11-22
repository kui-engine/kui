#ifndef KUI_C_EXAMPLE_RUNTIME_H
#define KUI_C_EXAMPLE_RUNTIME_H

#ifdef __cplusplus
#error "The canonical C examples must be compiled as C, not C++."
#endif

#include <kui_c.h>

typedef enum KuiCExampleKind {
    KUI_C_EXAMPLE_DOCUMENT = 0,
    KUI_C_EXAMPLE_LUA_ASSET = 1,
    KUI_C_EXAMPLE_THREE_D = 2,
    KUI_C_EXAMPLE_MEDIA = 3,
    KUI_C_EXAMPLE_NETWORK = 4,
    KUI_C_EXAMPLE_HELLO_EXTENSION = 5,
    KUI_C_EXAMPLE_SCRIPT = 6,
    KUI_C_EXAMPLE_FILE_HOST = 7,
    KUI_C_EXAMPLE_HTML_LOADER = 8
} KuiCExampleKind;

enum {
    KUI_C_EXAMPLE_EXTENSIONS = 1 << 0,
    KUI_C_EXAMPLE_DEVTOOLS = 1 << 1,
    KUI_C_EXAMPLE_FRAMELESS = 1 << 2,
    KUI_C_EXAMPLE_TRANSPARENT = 1 << 3,
    KUI_C_EXAMPLE_ALWAYS_ON_TOP = 1 << 4,
    /* 1 << 5 was "round corners"; no original sets a frame radius (see the generator). */
    KUI_C_EXAMPLE_MULTIWINDOW = 1 << 6,
    KUI_C_EXAMPLE_LOTTIE = 1 << 7,
    KUI_C_EXAMPLE_PDF = 1 << 8,
    KUI_C_EXAMPLE_TRAY = 1 << 9,
    KUI_C_EXAMPLE_FRAMELESS_SHADOW = 1 << 10,
    KUI_C_EXAMPLE_FRAMELESS_SIZEABLE = 1 << 11,
    KUI_C_EXAMPLE_CLOSE_TOGETHER = 1 << 12
};

/* What an example's host code gets once the page is loaded and the window is up (also given to
 * before_load, where the page is not loaded yet and the second window does not exist). The
 * runtime owns every handle here: use them, do not release them. */
typedef struct KuiCExampleHost {
    KuiWindow* window;
    KuiDocument* document;
    KuiWindow* second_window;     /* only with KUI_C_EXAMPLE_MULTIWINDOW, otherwise NULL */
    KuiDocument* second_document; /* idem */
    KuiScriptEngine* script;      /* bound to `document`; evaluates Lua (page scripts, form values) */
    int argc;                     /* the process's command line, as main() got it */
    char** argv;
} KuiCExampleHost;

/* The example's own host code (DOM lookups, event listeners, timers ...). Called once, on the
 * UI thread, after the page and the per-kind setup are done and before the event loop starts.
 * Return 0 on success; anything else aborts the example with exit code 9. */
typedef int (*KuiCExampleReadyFn)(const KuiCExampleHost* host);

/* Optional. Called once after the runtime has read the command line and before the platform and
 * the window exist: the place for environment variables KUI reads at start-up, the example's own
 * command line, and the window's creation properties (`props` is what kui_window_create gets).
 * Return 0 to go on; anything else ends the example right away with *exit_code (preset to 0),
 * e.g. after printing --help or --list. */
typedef int (*KuiCExamplePrepareFn)(int argc, char** argv, KuiWindowProps* props, int* exit_code);

/* Optional. Called once when the window, the document and the script engine exist (extensions
 * are initialised) but the page is not loaded yet: the place for custom elements, style
 * profiles, components. second_window / second_document are still NULL. Return 0 on success;
 * anything else aborts the example with exit code 9. */
typedef int (*KuiCExampleBeforeLoadFn)(const KuiCExampleHost* host);

typedef struct KuiCExampleSpec {
    const char* canonical_target;
    const char* title;
    int width;
    int height;
    KuiCExampleKind kind;
    int variant;
    unsigned flags;
    const char* asset;
    KuiCExampleReadyFn ready; /* NULL when the example is just the page */
    KuiCExamplePrepareFn prepare;       /* NULL unless the port defines it */
    KuiCExampleBeforeLoadFn before_load; /* idem */
} KuiCExampleSpec;

int kui_c_example_run(const KuiCExampleSpec* spec, int argc, char** argv);

/* Defined by the example's own ports/<target>.c (only linked into targets that have one); the
 * generator passes prepare / before_load only when the port defines them. */
int kui_c_example_ready(const KuiCExampleHost* host);
int kui_c_example_prepare(int argc, char** argv, KuiWindowProps* props, int* exit_code);
int kui_c_example_before_load(const KuiCExampleHost* host);

#endif
