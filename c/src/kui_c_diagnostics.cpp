// Section: process-wide host services -- the console, the stack trace writer and the crash reporter.
#include "kui_c_internal.h"

#include <kui/devtools/console.h>
#include <kui/diagnostics/crash_reporter.h>
#include <kui/diagnostics/stack_trace.h>

using kui_c::str;

namespace {
std::string optionalString(const char* text) { return text ? std::string(text) : std::string(); }
}  // namespace

extern "C" {

KuiStatus kui_console_set_enabled(int enabled) {
    KUI_C_BEGIN
        kui::Console::instance().setEnabled(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_console_set_stderr_output(int enabled) {
    KUI_C_BEGIN
        kui::Console::instance().setStderrOutput(enabled != 0);
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_console_set_min_level(KuiCLogLevel level) {
    if (level < KUI_C_LOG_DEBUG || level > KUI_C_LOG_ERROR) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::Console::instance().setMinLevel(static_cast<kui::LogLevel>(level));
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_stack_trace_install(const KuiCStackTraceConfig* config, int* out_installed) {
    if (out_installed) *out_installed = 0;
    if (config && config->struct_size < sizeof(KuiCStackTraceConfig)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::StackTrace::Config cfg;
        if (config) {
            if (config->output_mode != 0) cfg.outputMode = config->output_mode;
            cfg.logFilePath = optionalString(config->log_file_path);
            if (config->max_frames > 0) cfg.maxFrames = config->max_frames;
            if (config->app_name && config->app_name[0]) cfg.appName = config->app_name;
            cfg.writeMiniDump = config->write_mini_dump != 0;
            cfg.dumpDir = optionalString(config->dump_dir);
        }
        const bool installed = kui::StackTrace::install(cfg);
        if (out_installed) *out_installed = installed ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_stack_trace_uninstall(void) {
    KUI_C_BEGIN
        kui::StackTrace::uninstall();
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_crash_reporter_install(const KuiCCrashReporterConfig* config, int* out_installed) {
    if (out_installed) *out_installed = 0;
    if (config && config->struct_size < sizeof(KuiCCrashReporterConfig)) return KUI_ERR_INVALID_ARG;
    KUI_C_BEGIN
        kui::CrashReporter::Config cfg;
        if (config) {
            cfg.handlerExe = optionalString(config->handler_exe);
            cfg.dumpDir = optionalString(config->dump_dir);
            if (config->app_name && config->app_name[0]) cfg.appName = config->app_name;
            if (config->app_version && config->app_version[0]) cfg.appVersion = config->app_version;
            cfg.reportUrl = optionalString(config->report_url);
            cfg.enableUpload = config->enable_upload != 0;
        }
        const bool installed = kui::CrashReporter::install(cfg);
        if (out_installed) *out_installed = installed ? 1 : 0;
        return KUI_OK;
    KUI_C_END
}

KuiStatus kui_crash_reporter_uninstall(void) {
    KUI_C_BEGIN
        kui::CrashReporter::uninstall();
        return KUI_OK;
    KUI_C_END
}

}  // extern "C"
