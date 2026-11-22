/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_multi_window_animation.c; edit that file, not this copy. */
/* Host logic of examples/multi_window_animation/main.cpp: three 420x520 windows side by side,
 * centred on the primary monitor (CSS transitions, @keyframes, complex composited animations);
 * closing any window closes the other two.
 *
 * The generated page is the original's window 3, so this port loads window 1 into the main
 * window, turns the runner's second window into window 2 and creates window 3 itself. The ready
 * hook has no cleanup counterpart: window 3 is closed together with the others and its handle is
 * left to process exit (the runner shuts the platform down after the loop). */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static const char k_window1[] =
    "<html>\n"
    "<head>\n"
    "<style>\n"
    "body {\n"
    "    margin: 0; padding: 24px;\n"
    "    background: #0f172a; color: #e2e8f0;\n"
    "    font-family: 'Segoe UI', sans-serif;\n"
    "}\n"
    "h2 { color: #38bdf8; margin-bottom: 16px; font-size: 18px; }\n"
    ".info { color: #64748b; font-size: 12px; margin-bottom: 20px; }\n"
    "\n"
    ".card {\n"
    "    background: #1e293b;\n"
    "    border: 1px solid #334155;\n"
    "    border-radius: 8px;\n"
    "    padding: 16px;\n"
    "    margin-bottom: 12px;\n"
    "    transition: background 0.3s ease, border-color 0.3s ease, transform 0.2s ease;\n"
    "    cursor: pointer;\n"
    "}\n"
    ".card:hover {\n"
    "    background: #334155;\n"
    "    border-color: #38bdf8;\n"
    "    transform: translateX(8px);\n"
    "}\n"
    ".card-title {\n"
    "    font-size: 14px; font-weight: 600; color: #f1f5f9;\n"
    "    margin-bottom: 4px;\n"
    "}\n"
    ".card-desc { font-size: 12px; color: #94a3b8; }\n"
    "\n"
    ".btn-row { display: flex; gap: 8px; margin-top: 16px; }\n"
    ".btn {\n"
    "    padding: 8px 20px;\n"
    "    border-radius: 6px;\n"
    "    font-size: 13px;\n"
    "    cursor: pointer;\n"
    "    transition: background 0.2s ease, color 0.2s ease, transform 0.15s ease;\n"
    "    border: 1px solid transparent;\n"
    "}\n"
    ".btn-primary {\n"
    "    background: #2563eb; color: white; border-color: #3b82f6;\n"
    "}\n"
    ".btn-primary:hover {\n"
    "    background: #1d4ed8; transform: scale(1.05);\n"
    "}\n"
    ".btn-secondary {\n"
    "    background: #1e293b; color: #94a3b8; border-color: #475569;\n"
    "}\n"
    ".btn-secondary:hover {\n"
    "    background: #334155; color: #e2e8f0; transform: scale(1.05);\n"
    "}\n"
    "\n"
    ".progress-bar {\n"
    "    height: 6px; background: #1e293b; border-radius: 3px;\n"
    "    margin-top: 16px; overflow: hidden;\n"
    "}\n"
    ".progress-fill {\n"
    "    height: 100%; width: 0%;\n"
    "    background: linear-gradient(90deg, #38bdf8, #818cf8);\n"
    "    border-radius: 3px;\n"
    "    transition: width 2s ease-in-out;\n"
    "}\n"
    ".progress-fill.active { width: 100%; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "    <h2>Window 1 \342\200\224 CSS Transitions</h2>\n"
    "    <div class=\"info\">Hover over cards and buttons to see smooth CSS transitions</div>\n"
    "\n"
    "    <div class=\"card\">\n"
    "        <div class=\"card-title\">Smooth Background Transition</div>\n"
    "        <div class=\"card-desc\">background + border-color + transform on hover (0.3s ease)</div>\n"
    "    </div>\n"
    "    <div class=\"card\">\n"
    "        <div class=\"card-title\">Interactive Card Element</div>\n"
    "        <div class=\"card-desc\">Each property transitions independently with its own timing</div>\n"
    "    </div>\n"
    "    <div class=\"card\">\n"
    "        <div class=\"card-title\">Multi-Property Animation</div>\n"
    "        <div class=\"card-desc\">transform: translateX(8px) slides the card on hover</div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"btn-row\">\n"
    "        <div class=\"btn btn-primary\">Primary Action</div>\n"
    "        <div class=\"btn btn-secondary\">Secondary</div>\n"
    "        <div class=\"btn btn-secondary\">Cancel</div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"progress-bar\">\n"
    "        <div class=\"progress-fill active\"></div>\n"
    "    </div>\n"
    "</body>\n"
    "</html>\n";

static const char k_window2[] =
    "<html>\n"
    "<head>\n"
    "<style>\n"
    "body {\n"
    "    margin: 0; padding: 24px;\n"
    "    background: #0f172a; color: #e2e8f0;\n"
    "    font-family: 'Segoe UI', sans-serif;\n"
    "}\n"
    "h2 { color: #a78bfa; margin-bottom: 16px; font-size: 18px; }\n"
    ".info { color: #64748b; font-size: 12px; margin-bottom: 20px; }\n"
    "\n"
    ".anim-grid {\n"
    "    display: flex; flex-wrap: wrap; gap: 20px;\n"
    "    justify-content: center;\n"
    "}\n"
    ".anim-cell {\n"
    "    width: 120px; height: 120px;\n"
    "    display: flex; align-items: center; justify-content: center;\n"
    "    background: #1e293b; border-radius: 12px;\n"
    "    border: 1px solid #334155;\n"
    "}\n"
    ".anim-label {\n"
    "    text-align: center; font-size: 11px; color: #64748b;\n"
    "    margin-top: 6px;\n"
    "}\n"
    "\n"
    "@keyframes spin {\n"
    "    from { transform: rotate(0deg); }\n"
    "    to   { transform: rotate(360deg); }\n"
    "}\n"
    "@keyframes pulse {\n"
    "    0%, 100% { transform: scale(1); opacity: 1; }\n"
    "    50%      { transform: scale(1.3); opacity: 0.7; }\n"
    "}\n"
    "@keyframes bounce {\n"
    "    0%, 100% { transform: translateY(0); }\n"
    "    50%      { transform: translateY(-20px); }\n"
    "}\n"
    "@keyframes colorShift {\n"
    "    0%   { background: #ef4444; }\n"
    "    33%  { background: #22c55e; }\n"
    "    66%  { background: #3b82f6; }\n"
    "    100% { background: #ef4444; }\n"
    "}\n"
    "@keyframes slideIn {\n"
    "    from { transform: translateX(-40px); opacity: 0; }\n"
    "    to   { transform: translateX(0); opacity: 1; }\n"
    "}\n"
    "@keyframes fadeInOut {\n"
    "    0%, 100% { opacity: 0.3; }\n"
    "    50%      { opacity: 1; }\n"
    "}\n"
    "\n"
    ".spin-box {\n"
    "    width: 40px; height: 40px; background: #38bdf8; border-radius: 6px;\n"
    "    animation: spin 2s linear infinite;\n"
    "}\n"
    ".pulse-circle {\n"
    "    width: 40px; height: 40px; background: #a78bfa; border-radius: 50%;\n"
    "    animation: pulse 1.5s ease-in-out infinite;\n"
    "}\n"
    ".bounce-box {\n"
    "    width: 40px; height: 40px; background: #34d399; border-radius: 8px;\n"
    "    animation: bounce 1s ease-in-out infinite;\n"
    "}\n"
    ".color-box {\n"
    "    width: 40px; height: 40px; border-radius: 6px;\n"
    "    animation: colorShift 3s linear infinite;\n"
    "}\n"
    ".slide-box {\n"
    "    width: 40px; height: 40px; background: #fb923c; border-radius: 6px;\n"
    "    animation: slideIn 1.5s ease-out infinite;\n"
    "}\n"
    ".fade-circle {\n"
    "    width: 40px; height: 40px; background: #f472b6; border-radius: 50%;\n"
    "    animation: fadeInOut 2s ease-in-out infinite;\n"
    "}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "    <h2>Window 2 \342\200\224 @keyframes Animations</h2>\n"
    "    <div class=\"info\">Independent CSS animations running at 60fps</div>\n"
    "\n"
    "    <div class=\"anim-grid\">\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"spin-box\"></div></div>\n"
    "            <div class=\"anim-label\">spin (2s linear)</div>\n"
    "        </div>\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"pulse-circle\"></div></div>\n"
    "            <div class=\"anim-label\">pulse (1.5s ease)</div>\n"
    "        </div>\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"bounce-box\"></div></div>\n"
    "            <div class=\"anim-label\">bounce (1s ease)</div>\n"
    "        </div>\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"color-box\"></div></div>\n"
    "            <div class=\"anim-label\">colorShift (3s)</div>\n"
    "        </div>\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"slide-box\"></div></div>\n"
    "            <div class=\"anim-label\">slideIn (1.5s)</div>\n"
    "        </div>\n"
    "        <div>\n"
    "            <div class=\"anim-cell\"><div class=\"fade-circle\"></div></div>\n"
    "            <div class=\"anim-label\">fadeInOut (2s)</div>\n"
    "        </div>\n"
    "    </div>\n"
    "</body>\n"
    "</html>\n";

static const char k_window3[] =
    "<html>\n"
    "<head>\n"
    "<style>\n"
    "body {\n"
    "    margin: 0; padding: 24px;\n"
    "    background: #0f172a; color: #e2e8f0;\n"
    "    font-family: 'Segoe UI', sans-serif;\n"
    "}\n"
    "h2 { color: #f472b6; margin-bottom: 16px; font-size: 18px; }\n"
    ".info { color: #64748b; font-size: 12px; margin-bottom: 20px; }\n"
    "\n"
    "@keyframes orbit {\n"
    "    from { transform: rotate(0deg) translateX(60px) rotate(0deg); }\n"
    "    to   { transform: rotate(360deg) translateX(60px) rotate(-360deg); }\n"
    "}\n"
    "@keyframes glow {\n"
    "    0%, 100% { box-shadow: 0 0 8px #38bdf8; }\n"
    "    50%      { box-shadow: 0 0 24px #38bdf8; }\n"
    "}\n"
    "@keyframes expandWidth {\n"
    "    0%   { width: 40px; }\n"
    "    50%  { width: 200px; }\n"
    "    100% { width: 40px; }\n"
    "}\n"
    "@keyframes hueRotate {\n"
    "    0%   { background: #ef4444; }\n"
    "    25%  { background: #eab308; }\n"
    "    50%  { background: #22c55e; }\n"
    "    75%  { background: #3b82f6; }\n"
    "    100% { background: #ef4444; }\n"
    "}\n"
    "\n"
    ".orbit-container {\n"
    "    width: 160px; height: 160px; margin: 20px auto;\n"
    "    position: relative;\n"
    "}\n"
    ".orbit-center {\n"
    "    position: absolute;\n"
    "    left: 60px; top: 60px;\n"
    "    width: 40px; height: 40px;\n"
    "    background: #1e293b; border: 2px solid #38bdf8;\n"
    "    border-radius: 50%;\n"
    "    animation: glow 2s ease-in-out infinite;\n"
    "}\n"
    ".orbit-planet {\n"
    "    position: absolute;\n"
    "    left: 68px; top: 68px;\n"
    "    width: 24px; height: 24px;\n"
    "    background: #f472b6; border-radius: 50%;\n"
    "    animation: orbit 4s linear infinite;\n"
    "}\n"
    ".orbit-planet2 {\n"
    "    position: absolute;\n"
    "    left: 68px; top: 68px;\n"
    "    width: 16px; height: 16px;\n"
    "    background: #34d399; border-radius: 50%;\n"
    "    animation: orbit 2.5s linear infinite reverse;\n"
    "}\n"
    "\n"
    ".bar-row { display: flex; gap: 8px; align-items: center; margin-bottom: 12px; }\n"
    ".bar-label { width: 80px; font-size: 12px; color: #94a3b8; }\n"
    ".bar {\n"
    "    height: 24px; border-radius: 4px;\n"
    "    animation: expandWidth 3s ease-in-out infinite;\n"
    "}\n"
    ".bar.b1 { background: #ef4444; animation-delay: 0s; }\n"
    ".bar.b2 { background: #f59e0b; animation-delay: 0.4s; }\n"
    ".bar.b3 { background: #22c55e; animation-delay: 0.8s; }\n"
    ".bar.b4 { background: #3b82f6; animation-delay: 1.2s; }\n"
    "\n"
    ".color-strip {\n"
    "    height: 8px; border-radius: 4px; margin-top: 20px;\n"
    "    animation: hueRotate 4s linear infinite;\n"
    "}\n"
    "\n"
    ".stats-row {\n"
    "    display: flex; gap: 12px; margin-top: 20px;\n"
    "}\n"
    ".stat-card {\n"
    "    flex: 1; padding: 12px;\n"
    "    background: #1e293b; border-radius: 8px;\n"
    "    border: 1px solid #334155;\n"
    "    text-align: center;\n"
    "    transition: transform 0.2s ease, border-color 0.3s ease;\n"
    "}\n"
    ".stat-card:hover {\n"
    "    transform: scale(1.05);\n"
    "    border-color: #f472b6;\n"
    "}\n"
    ".stat-value { font-size: 20px; font-weight: 700; color: #f1f5f9; }\n"
    ".stat-label { font-size: 11px; color: #64748b; margin-top: 4px; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "    <h2>Window 3 \342\200\224 Complex Composited Animations</h2>\n"
    "    <div class=\"info\">Multi-transform orbit + animated bars + hover transitions</div>\n"
    "\n"
    "    <div class=\"orbit-container\">\n"
    "        <div class=\"orbit-center\"></div>\n"
    "        <div class=\"orbit-planet\"></div>\n"
    "        <div class=\"orbit-planet2\"></div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"bar-row\">\n"
    "        <div class=\"bar-label\">CPU</div>\n"
    "        <div class=\"bar b1\"></div>\n"
    "    </div>\n"
    "    <div class=\"bar-row\">\n"
    "        <div class=\"bar-label\">Memory</div>\n"
    "        <div class=\"bar b2\"></div>\n"
    "    </div>\n"
    "    <div class=\"bar-row\">\n"
    "        <div class=\"bar-label\">Network</div>\n"
    "        <div class=\"bar b3\"></div>\n"
    "    </div>\n"
    "    <div class=\"bar-row\">\n"
    "        <div class=\"bar-label\">Disk I/O</div>\n"
    "        <div class=\"bar b4\"></div>\n"
    "    </div>\n"
    "\n"
    "    <div class=\"color-strip\"></div>\n"
    "\n"
    "    <div class=\"stats-row\">\n"
    "        <div class=\"stat-card\">\n"
    "            <div class=\"stat-value\">60</div>\n"
    "            <div class=\"stat-label\">Target FPS</div>\n"
    "        </div>\n"
    "        <div class=\"stat-card\">\n"
    "            <div class=\"stat-value\">3</div>\n"
    "            <div class=\"stat-label\">Windows</div>\n"
    "        </div>\n"
    "        <div class=\"stat-card\">\n"
    "            <div class=\"stat-value\">12</div>\n"
    "            <div class=\"stat-label\">Animations</div>\n"
    "        </div>\n"
    "    </div>\n"
    "</body>\n"
    "</html>\n";

static KuiWindow* g_windows[3];
static int g_closing;

/* Closing any window closes the other two (the original's onClose). */
static void on_close(KuiEvent* event, void* user_data) {
    int index;
    (void)event;
    if (g_closing) return;
    g_closing = 1;
    for (index = 0; index < 3; ++index) {
        if (g_windows[index] && g_windows[index] != (KuiWindow*)user_data)
            kui_window_close(g_windows[index]);
    }
}

static void print_exit(void) {
    fprintf(stderr, "[MultiWindow] Event loop exited.\n");
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    static const char* const titles[3] = {
        "KUI \xE2\x80\x94 CSS Transitions", "KUI \xE2\x80\x94 @keyframes Animations",
        "KUI \xE2\x80\x94 Complex Composited"};
    const char* const pages[3] = {k_window1, k_window2, k_window3};
    const int win_w = 420, win_h = 520;
    KuiDocument* documents[3];
    KuiDocument* third = NULL;
    KuiWindowProps props;
    float screen_w = 0.0f, screen_h = 0.0f;
    int start_x, start_y, index;

    if (!host->second_window || !host->second_document) return 1;
    fprintf(stderr, "[MultiWindow] Starting multi-window animation demo...\n");
    kui_platform_set_f12_devtools_enabled(1);

    /* Platform::primaryMonitorSize(): 3 windows + gaps, centred. */
    kui_platform_get_primary_monitor_physical_size(&screen_w, &screen_h);
    start_x = ((int)screen_w - (win_w * 3 + 40)) / 2;
    start_y = ((int)screen_h - win_h) / 2;

    memset(&props, 0, sizeof props);
    props.title = titles[2];
    props.width = win_w;
    props.height = win_h;
    if (kui_window_create(&props, &g_windows[2]) != KUI_OK ||
        kui_window_get_document(g_windows[2], &third) != KUI_OK) {
        fprintf(stderr, "[%s] Failed to create window\n", titles[2]);
        return 1;
    }
    g_windows[0] = host->window;
    g_windows[1] = host->second_window;
    documents[0] = host->document;
    documents[1] = host->second_document;
    documents[2] = third;
    for (index = 0; index < 3; ++index) {
        kui_window_set_title(g_windows[index], titles[index], 0);
        kui_window_set_position(g_windows[index], start_x + (win_w + 20) * index, start_y);
        kui_document_load_html(documents[index], pages[index], 0);
        kui_window_add_event_listener(g_windows[index], "close", 0, on_close, g_windows[index], 0,
                                      NULL);
    }
    kui_document_release(third);
    atexit(print_exit);
    fprintf(stderr, "[MultiWindow] 3 windows created. Close any window to exit all.\n");
    return 0;
}
