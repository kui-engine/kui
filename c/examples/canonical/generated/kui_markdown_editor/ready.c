/* Copied by scripts/generate_c_examples.py from examples/c/ports/kui_markdown_editor.c; edit that file, not this copy. */
/* Host logic of examples/markdown_editor/main.cpp: the editor starts with the sample Markdown;
 * Clear empties it and Load Sample puts the sample back. After either, the preview shows the
 * converted HTML (as text, like the original: "for now, set as text") and #stats counts words
 * and characters of the content.
 *
 * The original never listens to the textarea: typing does not update the preview or the stats.
 * Its header checks compare a 3-char prefix with "### " and a 2-char prefix with "## ", so they
 * never match; this port does what they mean to (<h3> / <h2>). */
#include "kui_c_example_dom.h"

#include <stdlib.h>

static KuiDocument* g_doc;
static const char* g_content;

static const char k_default_markdown[] =
    "# Welcome to KUI Markdown Editor\n"
    "\n"
    "This is a **live preview** markdown editor built with KUI.\n"
    "\n"
    "## Features\n"
    "\n"
    "- Real-time markdown to HTML preview\n"
    "- Dark theme with syntax highlighting\n"
    "- Word and character counting\n"
    "- Split pane layout\n"
    "\n"
    "## Code Example\n"
    "\n"
    "```cpp\n"
    "#include <kui/kui.h>\n"
    "\n"
    "int main() {\n"
    "    kui::Platform::init();\n"
    "    auto window = kui::Window::create({.title = \"Hello\"});\n"
    "    return kui::Platform::run();\n"
    "}\n"
    "```\n"
    "\n"
    "## Formatting\n"
    "\n"
    "You can use **bold**, *italic*, and `inline code`.\n"
    "\n"
    "> Blockquotes are also supported for callouts and notes.\n"
    "\n"
    "---\n"
    "\n"
    "### Lists\n"
    "\n"
    "- Item one\n"
    "- Item two\n"
    "- Item three\n"
    "\n"
    "Enjoy writing with KUI!\n";

typedef struct Buf {
    char* data;
    size_t length, capacity;
} Buf;

static void buf_add(Buf* buf, const char* text, size_t length) {
    if (buf->length + length + 1 > buf->capacity) {
        size_t capacity = buf->capacity ? buf->capacity : 1024;
        char* grown;
        while (buf->length + length + 1 > capacity) capacity *= 2;
        grown = (char*)realloc(buf->data, capacity);
        if (!grown) return;
        buf->data = grown;
        buf->capacity = capacity;
    }
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
}

static void buf_puts(Buf* buf, const char* text) { buf_add(buf, text, strlen(text)); }

/* regex_replace(text, "<d>(.+?)<d>", "<open>$1<close>"): leftmost match, lazy body of >= 1 char. */
static void replace_pairs(Buf* out, const char* text, const char* delim,
                          const char* open, const char* close) {
    size_t d = strlen(delim);
    const char* p = text;
    while (*p) {
        const char* end = strncmp(p, delim, d) == 0 && p[d] ? strstr(p + d + 1, delim) : NULL;
        if (end) {
            buf_puts(out, open);
            buf_add(out, p + d, (size_t)(end - p - d));
            buf_puts(out, close);
            p = end + d;
        } else {
            buf_add(out, p++, 1);
        }
    }
}

static int starts(const char* line, const char* prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

/* The original's markdownToHTML (basic subset), line by line. */
static void markdown_to_html(Buf* html, const char* md) {
    int in_code = 0;
    while (*md) {
        const char* newline = strchr(md, '\n');
        size_t length = newline ? (size_t)(newline - md) : strlen(md);
        char* line = (char*)malloc(length + 1);
        if (!line) return;
        memcpy(line, md, length);
        line[length] = '\0';
        md += length + (newline ? 1 : 0);

        if (starts(line, "```")) {
            buf_puts(html, in_code ? "</pre>\n" : "<pre><code>");
            in_code = !in_code;
        } else if (in_code) {
            const char* c;
            for (c = line; *c; ++c) {
                if (*c == '<') buf_puts(html, "&lt;");
                else if (*c == '>') buf_puts(html, "&gt;");
                else if (*c == '&') buf_puts(html, "&amp;");
                else buf_add(html, c, 1);
            }
            buf_puts(html, "\n");
        } else if (!strcmp(line, "---") || !strcmp(line, "***") || !strcmp(line, "___")) {
            buf_puts(html, "<hr>\n");
        } else if (starts(line, "### ") || starts(line, "## ") || starts(line, "# ")) {
            const char* tag = starts(line, "### ") ? "h3" : starts(line, "## ") ? "h2" : "h1";
            char open[8], close[8];
            snprintf(open, sizeof open, "<%s>", tag);
            snprintf(close, sizeof close, "</%s>\n", tag);
            buf_puts(html, open);
            buf_puts(html, line + (tag[1] - '0') + 1);
            buf_puts(html, close);
        } else if (starts(line, "> ")) {
            buf_puts(html, "<blockquote><p>");
            buf_puts(html, line + 2);
            buf_puts(html, "</p></blockquote>\n");
        } else if (starts(line, "- ")) {
            buf_puts(html, "<li>");
            buf_puts(html, line + 2);
            buf_puts(html, "</li>\n");
        } else if (!line[0]) {
            buf_puts(html, "<br>\n");
        } else {
            Buf bold = {0}, italic = {0}, code = {0};
            replace_pairs(&bold, line, "**", "<strong>", "</strong>");
            replace_pairs(&italic, bold.data ? bold.data : "", "*", "<em>", "</em>");
            replace_pairs(&code, italic.data ? italic.data : "", "`", "<code>", "</code>");
            buf_puts(html, "<p>");
            buf_puts(html, code.data ? code.data : "");
            buf_puts(html, "</p>\n");
            free(bold.data);
            free(italic.data);
            free(code.data);
        }
        free(line);
    }
    if (in_code) buf_puts(html, "</pre>\n");
}

static void update_preview(void) {
    Buf html = {0};
    KuiElement* preview = kuex_by_id(g_doc, "preview");
    KuiElement* child = NULL;
    int words = 0, in_word = 0;
    const char* c;
    char stats[96];
    if (!preview) return;
    while (kui_element_first_child(preview, &child) == KUI_OK && child) {
        kui_element_remove_child(preview, child);
        kui_element_release(child);
        child = NULL;
    }
    markdown_to_html(&html, g_content);
    kuex_append_release(preview, kuex_make_element(g_doc, "div", html.data, NULL));
    kui_element_release(preview);
    free(html.data);

    for (c = g_content; *c; ++c) {
        if (*c == ' ' || *c == '\n' || *c == '\r' || *c == '\t') {
            if (in_word) ++words;
            in_word = 0;
        } else {
            in_word = 1;
        }
    }
    if (in_word) ++words;
    snprintf(stats, sizeof stats, "%d words, %d characters", words, (int)strlen(g_content));
    kuex_set_text(g_doc, "stats", stats);
}

static void on_button(KuiEvent* event, void* user_data) {
    (void)event;
    g_content = user_data ? k_default_markdown : "";
    kuex_set_text(g_doc, "editor", g_content);
    update_preview();
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    g_doc = host->document;
    g_content = k_default_markdown;
    kuex_set_text(g_doc, "editor", g_content);
    if (kuex_on(g_doc, "btn-clear", "click", on_button, NULL) |
        kuex_on(g_doc, "btn-sample", "click", on_button, (void*)k_default_markdown)) {
        return 1;
    }
    update_preview();
    return 0;
}
