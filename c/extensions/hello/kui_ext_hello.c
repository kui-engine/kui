// ===========================================================================
// kui_ext_hello.c — Minimal example extension DLL
//
// Registers a <kui-hello> custom element that draws a colored rectangle
// with greeting text using the Host API DisplayList functions.
//
// Build: produces kui_ext_hello.dll
// ===========================================================================

#include <kui/extension/kui_extension_api.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static const KuiExtHostAPI* g_host = NULL;
static int g_frameCount = 0;

// ---------------------------------------------------------------------------
// Lifecycle callbacks
// ---------------------------------------------------------------------------
static void onConnected(void* element) {
    if (g_host && g_host->logInfo) {
        g_host->logInfo("kui-hello: connected to DOM");
    }
}

static void onDisconnected(void* element) {
    if (g_host && g_host->logInfo) {
        g_host->logInfo("kui-hello: disconnected from DOM");
    }
}

static void onAttributeChanged(void* element, const char* name,
                                const char* oldVal, const char* newVal) {
    if (g_host && g_host->logInfo) {
        char buf[256];
        snprintf(buf, sizeof(buf), "kui-hello: attr '%s' changed: '%s' -> '%s'",
                 name, oldVal ? oldVal : "(null)", newVal ? newVal : "(null)");
        g_host->logInfo(buf);
    }
    if (g_host && g_host->scheduleRepaint) {
        g_host->scheduleRepaint(element);
    }
}

// ---------------------------------------------------------------------------
// Paint callback — draw colored background + greeting text
// ---------------------------------------------------------------------------
static void onPaint(KuiExtPaintContext* ctx) {
    if (!ctx || !g_host) return;

    // Draw background rectangle (teal color)
    if (g_host->dlFillRect) {
        g_host->dlFillRect(ctx->displayList,
                           0, 0, ctx->contentWidth, ctx->contentHeight,
                           0, 128, 128, 255);
    }

    // Draw greeting text
    if (g_host->dlDrawText) {
        const char* greeting = "Hello from Extension!";
        if (g_host->getAttribute) {
            const char* name = g_host->getAttribute(ctx->element, "name");
            if (name && name[0]) {
                static char textBuf[256];
                snprintf(textBuf, sizeof(textBuf), "Hello, %s!", name);
                greeting = textBuf;
            }
        }
        g_host->dlDrawText(ctx->displayList, greeting,
                           10.0f, 24.0f,
                           "sans-serif", 16.0f,
                           255, 255, 255, 255);
    }
}

// ---------------------------------------------------------------------------
// Tick callback — increment frame counter
// ---------------------------------------------------------------------------
static void onTick(void* element, double dt) {
    g_frameCount++;
}

static int onNeedsTick(void* element) {
    // Only tick for the first 300 frames (~5 seconds at 60fps)
    return g_frameCount < 300;
}

// ---------------------------------------------------------------------------
// Lua method: getFrameCount() — demonstrates typed value marshalling
// ---------------------------------------------------------------------------
static int luaGetFrameCount(void* element, int argc, const KuiValue* argv,
                             KuiValue* results, int maxResults) {
    if (maxResults < 1) return 0;
    results[0].type = KUI_VAL_INT;
    results[0].intVal = g_frameCount;
    return 1;
}

// Lua method: greet(name) — returns "Hello, <name>!"
static int luaGreet(void* element, int argc, const KuiValue* argv,
                     KuiValue* results, int maxResults) {
    if (maxResults < 1) return 0;
    const char* name = "World";
    if (argc > 0 && argv[0].type == KUI_VAL_STRING && argv[0].strVal) {
        name = argv[0].strVal;
    }
    static char greetBuf[256];
    snprintf(greetBuf, sizeof(greetBuf), "Hello, %s!", name);
    results[0].type = KUI_VAL_STRING;
    results[0].strVal = greetBuf;
    return 1;
}

// ---------------------------------------------------------------------------
// Lua method table
// ---------------------------------------------------------------------------
static const KuiExtLuaMethodDef g_luaMethods[] = {
    { "getFrameCount", luaGetFrameCount, 0, 0 },
    { "greet",         luaGreet,         0, 1 },
};

// ===========================================================================
// DLL exports
// ===========================================================================
KUI_EXT_API KuiExtPluginInfo kuiext_pluginInfo(void) {
    KuiExtPluginInfo info;
    info.major = 1;
    info.minor = 0;
    info.patch = 0;
    info.name = "kui_ext_hello";
    info.tagName = "kui-hello";
    info.description = "Minimal example extension for KUI";
    info.author = "KUI Team";
    return info;
}

KUI_EXT_API KuiExtError kuiext_init(const KuiExtHostAPI* api) {
    if (!api) return KUI_EXT_ERR_INIT;
    if (api->apiVersion < 1) return KUI_EXT_ERR_VERSION;
    g_host = api;
    g_frameCount = 0;
    return KUI_EXT_OK;
}

KUI_EXT_API KuiExtRegistration kuiext_register(void) {
    KuiExtRegistration reg;
    memset(&reg, 0, sizeof(reg));

    reg.info = kuiext_pluginInfo();
    reg.onConnected = onConnected;
    reg.onDisconnected = onDisconnected;
    reg.onAttributeChanged = onAttributeChanged;
    reg.onPaint = onPaint;
    reg.onTick = onTick;
    reg.onNeedsTick = onNeedsTick;
    reg.onIntrinsicSize = NULL;

    reg.luaMethodCount = 2;
    reg.luaMethods = g_luaMethods;
    reg.onRegisterLua = NULL;

    return reg;
}

KUI_EXT_API void kuiext_shutdown(void) {
    g_host = NULL;
    g_frameCount = 0;
}

KUI_EXT_API const char* kuiext_getError(void) {
    return NULL;
}
