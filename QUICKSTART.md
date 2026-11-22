# KUI from C: quickstart

**English** · [简体中文](QUICKSTART.zh-CN.md)

[中文](QUICKSTART.zh-CN.md)

Build a native window from an HTML page in plain C: a card with a small form and a counter that goes up each time you click the button.

<p align="center"><img src="docs/media/quickstart.gif" width="360" alt="The quickstart window, recorded: typing a name, picking a plan and a date, switching options, dragging the slider and clicking the button three times"></p>
<p align="center"><sub>A real window, recorded: edit the form, then click the button three times.</sub></p>

The page is ordinary HTML and CSS. Next to the counter it has a small form (text input, select, date picker, checkbox, radio buttons and a slider). KUI draws and handles those controls itself, so they work without any code: the recording above types a name, picks a plan and a date, switches the options, drags the slider and clicks the button.

The program uses `kui_c.h` and the `find_package(KuiC)` config in this repository's `cmake/`. The full API is in
`c/include/kui_c.h`, the conventions in `c/README.md`.

## 1. What you need

- Visual Studio 2022 with the C/C++ workload (MSVC), CMake 3.20 or newer, Git.
- Verified on Windows 10 x64 with MSVC 19.43 (Visual Studio 2022 17.13), CMake 3.31.6 and Git 2.45.

> A plain PowerShell is enough: CMake finds Visual Studio by itself.

## 2. Get the SDK

```powershell
git clone --depth 1 https://github.com/kui-engine/kui.git
$KUI = "$PWD\kui"
```

For a fixed version: `git clone --depth 1 --branch v3.61.8 https://github.com/kui-engine/kui.git`.

> Keep `--depth 1`: every version commits its runtimes, so the full history is large.

## 3. Create an empty project

Next to the clone, create a folder `hello-kui` with three files: `CMakeLists.txt`, `main.c` and `app.html`.

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello_kui LANGUAGES C)

find_package(KuiC 3 CONFIG REQUIRED)

add_executable(hello_kui main.c)
target_link_libraries(hello_kui PRIVATE KuiC::C)
kui_c_copy_runtime_dlls(hello_kui)

# Put the page next to the program after every build.
add_custom_command(TARGET hello_kui POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/app.html" "$<TARGET_FILE_DIR:hello_kui>"
    VERBATIM)
```

```c
#include <kui_c.h>

#include <stdio.h>
#include <string.h>

typedef struct App {
    KuiElement* count; /* #count, looked up once */
    int clicks;
} App;

/* evt is borrowed: valid only during this call, never store or release it. */
static void on_click(KuiEvent* evt, void* user_data) {
    App* app = (App*)user_data;
    char text[16];
    (void)evt;
    snprintf(text, sizeof text, "%d", ++app->clicks);
    kui_element_set_text_content(app->count, text, 0); /* the DOM update */
}

int main(void) {
    KuiWindowProps props;
    KuiWindow* win = NULL;
    KuiDocument* doc = NULL;
    KuiElement* button = NULL;
    App app = {NULL, 0};
    int exit_code = 0;

    if (kui_platform_init() != KUI_OK) return 1;

    memset(&props, 0, sizeof props); /* 0 = library default for every field */
    props.title = "KUI Quick Start";
    props.width = 560; /* client area */
    props.height = 640;
    props.no_resize = 1;
    props.centered = 1;
    if (kui_window_create(&props, &win) != KUI_OK) return 1;

    /* The window owns its document; this hands back a counted reference. */
    kui_window_get_document(win, &doc);
    /* A relative path is resolved against the current directory. */
    kui_document_load_html_file(doc, "app.html", 0);
    if (kui_document_get_element_by_id(doc, "count", 0, &app.count) != KUI_OK) {
        fprintf(stderr, "app.html not found in the current directory\n");
        exit_code = 1;
    } else {
        kui_document_get_element_by_id(doc, "inc", 0, &button);
        kui_element_add_event_listener(button, "click", 0, on_click, &app, NULL);
        kui_platform_run(&exit_code); /* blocks until the window is closed */
        kui_element_release(button);
        kui_element_release(app.count);
    }

    kui_document_release(doc);
    kui_window_release(win); /* destroys the window */
    kui_platform_shutdown();
    return exit_code;
}
```

<details><summary>app.html (the interface, copy as is)</summary>

```html
﻿<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>KUI Quick Start</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { height: 100%; }
  body {
    display: flex; align-items: center; justify-content: center;
    font-family: "Segoe UI", system-ui, -apple-system, "Helvetica Neue", Arial, sans-serif;
    font-size: 14px; color: #e2e8f0;
    background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 55%, #312e81 100%);
  }
  .card {
    display: flex; flex-direction: column;
    width: 480px; padding: 28px 30px 20px;
    border-radius: 24px;
    background: rgba(255, 255, 255, 0.07);
    border: 1px solid rgba(255, 255, 255, 0.14);
    box-shadow: 0 24px 60px rgba(0, 0, 0, 0.45);
  }
  .head { display: flex; flex-direction: column; align-items: center; text-align: center; }
  .badge {
    display: inline-block; padding: 4px 14px; border-radius: 999px;
    font-size: 12px; font-weight: 600; letter-spacing: 2px;
    color: #c7d2fe; background: rgba(99, 102, 241, 0.28);
  }
  h1 { margin-top: 12px; font-size: 26px; font-weight: 700; color: #ffffff; }
  .sub { margin-top: 6px; font-size: 14px; color: #94a3b8; }

  .field { display: flex; flex-direction: column; margin-top: 16px; }
  .field label { margin-bottom: 6px; font-size: 12px; font-weight: 600; letter-spacing: 0.5px; color: #a5b4fc; }
  .pair { display: flex; gap: 14px; }
  .pair .field { flex: 1; }
  input[type="text"], input[type="date"], select {
    width: 100%; padding: 9px 12px; font-size: 14px; color: #f1f5f9;
    background: rgba(15, 23, 42, 0.55);
    border: 1px solid rgba(165, 180, 252, 0.28); border-radius: 10px;
  }
  input[type="text"]:focus, input[type="date"]:focus, select:focus { border-color: #818cf8; outline: none; }

  .opts { display: flex; align-items: center; justify-content: space-between; margin-top: 16px; }
  .opts label { display: flex; align-items: center; font-size: 14px; color: #cbd5e1; }
  .opts input { margin-right: 8px; }
  .radios { display: flex; gap: 16px; }
  input[type="range"] { width: 100%; }

  .action { display: flex; align-items: center; justify-content: space-between; margin-top: 22px; padding-top: 18px;
            border-top: 1px solid rgba(255, 255, 255, 0.1); }
  .counter { display: flex; align-items: baseline; }
  #count { font-size: 44px; font-weight: 800; line-height: 1; color: #a5b4fc; }
  #label { margin-left: 8px; font-size: 14px; color: #94a3b8; }
  #inc {
    padding: 12px 30px; border: 0; border-radius: 14px;
    font-size: 16px; font-weight: 600; color: #ffffff;
    background: linear-gradient(135deg, #6366f1, #8b5cf6);
    box-shadow: 0 10px 24px rgba(99, 102, 241, 0.45);
    transition: transform 0.15s, box-shadow 0.15s;
  }
  #inc:hover { transform: translateY(-2px); box-shadow: 0 14px 30px rgba(99, 102, 241, 0.6); }
  #inc:active { transform: translateY(1px); }
  .foot { margin-top: 18px; text-align: center; font-size: 12px; color: #64748b; }
</style>
</head>
<body>
  <div class="card">
    <div class="head">
      <span class="badge">KUI</span>
      <h1>Hello from KUI</h1>
      <p class="sub">A native window drawn from HTML and CSS.</p>
    </div>

    <div class="field">
      <label>NAME</label>
      <input id="name" type="text" value="Ada Lovelace">
    </div>
    <div class="pair">
      <div class="field">
        <label>PLAN</label>
        <select id="plan">
          <option>Free</option>
          <option selected>Pro</option>
          <option>Team</option>
        </select>
      </div>
      <div class="field">
        <label>START DATE</label>
        <input id="start" type="date" value="2026-12-01">
      </div>
    </div>
    <div class="opts">
      <label><input id="news" type="checkbox" checked>Email me updates</label>
      <div class="radios">
        <label><input type="radio" name="billing" checked>Monthly</label>
        <label><input type="radio" name="billing">Yearly</label>
      </div>
    </div>
    <div class="field">
      <label>VOLUME</label>
      <input id="volume" type="range" min="0" max="100" value="50">
    </div>

    <div class="action">
      <div class="counter"><div id="count">0</div><div id="label">clicks</div></div>
      <button id="inc">Click me</button>
    </div>
    <div class="foot">One engine · Windows, Linux, macOS</div>
  </div>
</body>
</html>
```

</details>

> Every call returns a `KuiStatus` (`KUI_OK` is 0, `kui_status_message()` describes the others). To stay short, the
> program checks only the calls whose failure it can act on; check every call in real code.

## 4. Where the runtime lives

`find_package(KuiC)` reads `$KUI/cmake/KuiCConfig.cmake`. It takes the headers from `$KUI/c/include` and the runtime
from `$KUI/runtime/windows-x64/`: `bin/` holds `kui_c.dll`, `kui.dll` and the plugins, `lib/` the import library.
Windows looks for DLLs next to the exe, so `kui_c_copy_runtime_dlls(hello_kui)` copies them into the output folder
after each build; the custom command does the same for `app.html`. To ship the program, copy the whole output folder.

## 5. A minimal window

1. `kui_platform_init()` on the thread that runs the UI. Every function except `kui_platform_post_to_main_thread` must
   be called on that thread.
2. Zero `KuiWindowProps` and set only what you need; `width` and `height` are the client area. The window is already
   visible when `kui_window_create` returns.
3. `kui_window_get_document` hands back the window's document; `kui_document_load_html_file` loads the page into it.

## 6. One DOM update

`kui_document_get_element_by_id` finds `#count` once and keeps the handle in `app`. On each click,
`kui_element_set_text_content` replaces its text with the new number. Strings are UTF-8; a length of 0 means
NUL-terminated. Reading text back uses a two-call pattern, see `c/examples/hello_world.c`.

## 7. One event listener

A listener is a plain C function, and state reaches it through `user_data` (here `&app`). It runs on the main thread,
and changing the DOM inside it is allowed. To remove it later, pass a `KuiListenerId*` instead of NULL and call
`kui_element_remove_event_listener` with the event name and that id.

## 8. Releasing resources

- Document and element handles are reference counted: release each one you got exactly once, then stop using it.
- The window is uniquely owned: `kui_window_release` destroys it. Your document reference outlives the window, so
  release it too.
- A `KuiEvent*` is borrowed and never released; a handle taken from it (`kui_event_get_target`) must be released.
- KUI never frees `user_data`; keep it alive while the listener can fire.
- Release everything after `kui_platform_run` returns and before `kui_platform_shutdown`.

## 9. Build and run

From the folder that holds the clone and `hello-kui`, in Windows PowerShell:

```powershell
cd hello-kui
cmake -S . -B build "-DKuiC_DIR=$KUI/cmake"
cmake --build build --config Release --parallel 4
cd build\Release
.\hello_kui.exe
```

The "KUI Quick Start" window opens and the number goes up with each click. Closing the window ends the program.

> The program is a console program, so a console window opens with it; error messages go there.

Linux and macOS: this page is verified on Windows x64 only. The differences are the runtime folder
(`$KUI/runtime/linux-x64/lib` or `$KUI/runtime/macos-arm64/lib`), the compiler (gcc / clang, Apple clang), a
single-config build (`-DCMAKE_BUILD_TYPE=Release`, program in `build/`), and an absolute RPATH into the clone instead of
copied libraries. The top-level `README.md`, section "Building and running a C example", has commands verified on all
three platforms.

## 10. Common problems

- **"app.html not found in the current directory", or a blank window.** The page path is relative to the current
  directory, not to the exe. Start the program from its own folder (double-clicking it in Explorer does that), or pass
  an absolute path. `kui_document_load_html_file` currently reports success even for a missing file, which is why the
  program checks for `#count` instead.
- **"kui_c.dll was not found" (a system error box, or exit code `0xc0000135`).** The DLLs are not next to the exe:
  keep `kui_c_copy_runtime_dlls(...)`, or copy `$KUI/runtime/windows-x64/bin/*` next to the exe.
- **Debug builds.** Only Release runtimes ship. `--config Debug` links the same `kui_c.dll`; that is fine because no
  CRT objects cross the C ABI (`/MD`, `/MDd` and `/MTd` apps all work).
- **Hundreds of compile errors on a Chinese-locale Windows.** The headers have UTF-8 comments and MSVC needs `/utf-8`;
  `KuiC::C` adds it, so link the target instead of only adding the include path.
