/* Main-thread hand-off regression tests (kui_platform_post_to_main_thread and
 * kui_http_client_fetch_async_on_main_thread). C11, like the other C ABI tests;
 * a separate executable because it drives the whole platform lifecycle
 * (refused before init, init, cross-thread posts, shutdown with work still
 * queued, refused after shutdown). */
#include <kui_c.h>

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef DWORD thread_id_t;
static thread_id_t current_thread(void) { return GetCurrentThreadId(); }
#else
#include <pthread.h>
typedef pthread_t thread_id_t;
static thread_id_t current_thread(void) { return pthread_self(); }
#endif

static int passed = 0;
static int failed = 0;

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (condition) ++passed;                                               \
        else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, message); } \
    } while (0)

#define CHECK_OK(expression, message) CHECK((expression) == KUI_OK, message)

enum { kWorkerPosts = 64 };

static thread_id_t main_thread;
static volatile long ran_on_other_thread = 0;

typedef struct Counter {
    int ran;
    int discarded;
    int order[kWorkerPosts];
    int order_len;
} Counter;

typedef struct Item {
    Counter* counter;
    int index;
} Item;

static void note_thread(void) {
    if (current_thread() != main_thread) ran_on_other_thread = 1;
}

static void on_run(void* user_data) {
    Item* item = (Item*)user_data;
    note_thread();
    ++item->counter->ran;
    if (item->counter->order_len < kWorkerPosts)
        item->counter->order[item->counter->order_len++] = item->index;
}

static void on_discard(void* user_data) {
    Item* item = (Item*)user_data;
    note_thread();
    ++item->counter->discarded;
}

static void pump_until(const int* value, int expected) {
    for (int i = 0; i < 400 && *value < expected; ++i)
        kui_platform_wait_events(0.01);
}

/* --- worker thread that posts kWorkerPosts items in order ------------------- */
static Item worker_items[kWorkerPosts];
static volatile long worker_failures = 0;

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID arg)
#else
static void* worker_main(void* arg)
#endif
{
    (void)arg;
    for (int i = 0; i < kWorkerPosts; ++i) {
        if (kui_platform_post_to_main_thread(on_run, on_discard, &worker_items[i]) != KUI_OK)
            ++worker_failures;
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void run_worker_and_join(void) {
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
    CHECK(thread != NULL, "worker thread starts");
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
#else
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, worker_main, NULL) == 0, "worker thread starts");
    pthread_join(thread, NULL);
#endif
}

/* --- HTTP on the main thread ------------------------------------------------- */
typedef struct HttpResult {
    int calls;
    int had_response;
    int status;
} HttpResult;

static void on_http(KuiHttpResponse* response, void* user_data) {
    HttpResult* result = (HttpResult*)user_data;
    note_thread();
    ++result->calls;
    if (response) {
        result->had_response = 1;
        kui_http_response_get_status(response, &result->status);
        kui_http_response_release(response);
    }
}

int main(void) {
    main_thread = current_thread();
    Counter counter;
    memset(&counter, 0, sizeof counter);
    Item early = {&counter, -1};

    /* Refused before init: neither callback fires, the caller keeps user_data. */
    CHECK(kui_platform_post_to_main_thread(on_run, on_discard, &early) == KUI_ERR_NOT_SUPPORTED,
          "post before init is refused");
    CHECK(kui_platform_post_to_main_thread(NULL, NULL, &early) == KUI_ERR_INVALID_ARG,
          "post validates fn");

    CHECK_OK(kui_platform_init(), "platform init");

    /* Posted from the main thread: never runs synchronously. */
    Item self = {&counter, -1};
    CHECK_OK(kui_platform_post_to_main_thread(on_run, on_discard, &self), "post from main thread");
    CHECK(counter.ran == 0, "post from main thread runs asynchronously");
    pump_until(&counter.ran, 1);
    CHECK(counter.ran == 1, "post from main thread runs from the event loop");

    /* Posted from a worker: all run on the main thread, in posting order. */
    memset(&counter, 0, sizeof counter);
    for (int i = 0; i < kWorkerPosts; ++i) {
        worker_items[i].counter = &counter;
        worker_items[i].index = i;
    }
    run_worker_and_join();
    CHECK(worker_failures == 0, "worker posts are accepted");
    pump_until(&counter.ran, kWorkerPosts);
    CHECK(counter.ran == kWorkerPosts, "every worker post runs");
    CHECK(counter.discarded == 0, "nothing discarded while running");
    int in_order = counter.order_len == kWorkerPosts;
    for (int i = 0; in_order && i < kWorkerPosts; ++i) in_order = counter.order[i] == i;
    CHECK(in_order, "posts from one thread run in posting order");

    /* HTTP: connection refused on a closed local port completes quickly; the
     * callback must land on the main thread with the response. */
    KuiHttpClient* client = NULL;
    CHECK_OK(kui_http_client_create(&client), "http client create");
    HttpResult http = {0, 0, -1};
    CHECK_OK(kui_http_client_fetch_async_on_main_thread(client, "http://127.0.0.1:1/", 0, 5.0,
                                                        on_http, &http),
             "main-thread fetch starts");
    CHECK(kui_http_client_fetch_async_on_main_thread(client, "http://127.0.0.1:1/", 0, 5.0,
                                                     NULL, &http) == KUI_ERR_INVALID_ARG,
          "main-thread fetch validates fn");
    for (int i = 0; i < 1000 && http.calls == 0; ++i) kui_platform_wait_events(0.01);
    CHECK(http.calls == 1, "main-thread fetch calls back once");
    CHECK(http.had_response, "main-thread fetch delivers a response object");
    kui_http_client_release(client);

    /* Still queued at shutdown: discarded on the shutdown thread, not run. */
    memset(&counter, 0, sizeof counter);
    for (int i = 0; i < kWorkerPosts; ++i) {
        worker_items[i].counter = &counter;
        worker_items[i].index = i;
    }
    run_worker_and_join();
    CHECK(worker_failures == 0, "worker posts before shutdown are accepted");
    CHECK_OK(kui_platform_shutdown(), "platform shutdown");
    CHECK(counter.ran == 0, "queued posts do not run during shutdown");
    CHECK(counter.discarded == kWorkerPosts, "every queued post is discarded exactly once");

    /* Refused after shutdown. */
    CHECK(kui_platform_post_to_main_thread(on_run, on_discard, &early) == KUI_ERR_NOT_SUPPORTED,
          "post after shutdown is refused");
    KuiHttpClient* late = NULL;
    if (kui_http_client_create(&late) == KUI_OK) {
        CHECK(kui_http_client_fetch_async_on_main_thread(late, "http://127.0.0.1:1/", 0, 1.0,
                                                         on_http, &http) == KUI_ERR_NOT_SUPPORTED,
              "main-thread fetch after shutdown is refused");
        kui_http_client_release(late);
    }

    CHECK(ran_on_other_thread == 0, "callbacks only ever run on the main thread");
    CHECK(counter.ran == 0 && counter.discarded == kWorkerPosts, "refused posts fire nothing");

    printf("kui_c threading: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
