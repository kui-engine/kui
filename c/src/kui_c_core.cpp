// =============================================================================
// kui_c_core.cpp — version and status reporting
// =============================================================================
#include "kui_c_internal.h"

#include <kui/version.h>

extern "C" {

KuiVersion kui_get_version(void) {
    // Reports the version of the loaded library, which may differ from the
    // KUI_C_API_VERSION_* macros the caller compiled against.
    KuiVersion v;
    v.major = kui::versionMajor;
    v.minor = kui::versionMinor;
    v.patch = kui::versionPatch;
    return v;
}

const char* kui_status_message(KuiStatus s) {
    // Static storage, never NULL, no allocation -- safe to call from an error
    // path that is already out of memory.
    switch (s) {
        case KUI_OK:                   return "OK";
        case KUI_ERR_INVALID_ARG:      return "invalid argument";
        case KUI_ERR_OUT_OF_MEMORY:    return "out of memory";
        case KUI_ERR_NOT_FOUND:        return "not found";
        case KUI_ERR_NOT_SUPPORTED:    return "not supported";
        case KUI_ERR_PARSE:            return "parse error";
        case KUI_ERR_IO:               return "I/O error";
        case KUI_ERR_PLUGIN_MISSING:   return "required plugin is missing";
        case KUI_ERR_BUFFER_TOO_SMALL: return "output buffer too small";
        case KUI_ERR_INTERNAL:         return "internal error";
    }
    // Reached only if a caller invents a value; no default: above so that adding
    // a status code without a message here is a compiler warning.
    return "unknown status";
}

}  // extern "C"
