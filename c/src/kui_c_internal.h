// =============================================================================
// kui_c_internal.h — shared plumbing for the C ABI implementation
//
// Not installed; consumers only ever see <kui_c.h>.
//
// Three jobs, each of which the ABI depends on getting exactly right:
//   1. handle <-> C++ pointer conversion
//   2. the exception boundary (no C++ exception may escape a C function)
//   3. string marshalling in both directions, without allocating for the caller
// =============================================================================
#pragma once

#include <kui_c.h>

#include <kui/core/types.h>

#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace kui_c {

// --- 0. Main-thread hand-off ---------------------------------------------------
// Shared by kui_platform_post_to_main_thread and the main-thread HTTP variant.
// A task runs `run` on the main thread, or -- if it is still queued when the
// platform shuts down and the queue is dropped -- `discard` from the destructor,
// on the shutdown thread. Exactly one of them fires; neither fires if the post
// is refused. Only a shared_ptr crosses into kui::Callback (its inline storage is
// relocated with memcpy, which std::function captures would not survive).
struct MainThreadTask {
    std::function<void()> run;
    std::function<void()> discard;
    bool settled = false;  // run or discard already fired, or the post was refused

    ~MainThreadTask() {
        if (!settled && discard) {
            try {
                discard();
            } catch (...) {
            }
        }
    }
};

// Thread-safe. Returns false (and marks the task settled) when the platform is
// not running -- before kui_platform_init or once kui_platform_shutdown started.
bool postToMainThread(const std::shared_ptr<MainThreadTask>& task);
// Thread-safe snapshot of the same state; may change right after it returns.
bool platformRunning();

// Drops the C-side per-window state (menu / tray definitions and callbacks) of a
// destroyed kui::Window. Called by kui_window_release after the window is gone.
void forgetWindow(void* window);

// --- 0b. user_data ownership (the release_fn rule of kui_c.h section 11) --------
// Holds user_data; calls release_fn once, when the last owner lets go.
struct UserData {
    void* data;
    KuiTaskFn release;
    UserData(void* value, KuiTaskFn fn) : data(value), release(fn) {}
    ~UserData() {
        if (release) release(data);
    }
    UserData(const UserData&) = delete;
    UserData& operator=(const UserData&) = delete;
};
using UserDataPtr = std::shared_ptr<UserData>;

// Disarms the holder unless the call reached commit(): a failed call (or a C++
// exception caught by KUI_C_END) leaves user_data with the caller.
struct ArmOnSuccess {
    UserDataPtr holder;
    bool committed = false;
    explicit ArmOnSuccess(UserDataPtr value) : holder(std::move(value)) {}
    ~ArmOnSuccess() {
        if (!committed) holder->release = nullptr;
    }
    void commit() { committed = true; }
};

// Deferred free for objects released inside their own callback.
template <typename T>
void freeLater(T* object) {
    auto task = std::make_shared<MainThreadTask>();
    task->run = [object]() { delete object; };
    task->discard = [object]() { delete object; };
    postToMainThread(task);  // refused: leak rather than free a live frame
}

// T has `int callbackDepth` and `bool releasedInCallback`; the release path sets
// releasedInCallback instead of deleting while callbackDepth > 0.
template <typename T>
struct CallbackScope {
    T* object;
    explicit CallbackScope(T* value) : object(value) { ++object->callbackDepth; }
    ~CallbackScope() {
        if (--object->callbackDepth == 0 && object->releasedInCallback) {
            object->releasedInCallback = false;
            freeLater(object);
        }
    }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
};

// --- 1. Handles ---------------------------------------------------------------
// A handle IS the object pointer, reinterpret_cast'd. Zero indirection, zero
// allocation, no handle table. Validity comes from reference counting instead:
// every function that returns a handle takes a reference (kui::Node derives from
// RefCounted, whose ref()/deref() are atomic), so the object cannot be destroyed
// while the C side still holds a handle -- even after it leaves the DOM tree.
//
// The cast is only sound if the handle really came from this layer. C's type
// system keeps KuiDocument* and KuiElement* apart, which is the protection we
// get; a caller who casts between them deliberately is in undefined behaviour,
// the same as anywhere else in C.
template <typename T, typename H>
inline T* fromHandle(H* h) {
    return reinterpret_cast<T*>(h);
}

template <typename H, typename T>
inline H* toHandle(T* p) {
    return reinterpret_cast<H*>(p);
}

// --- 2. Exception boundary ----------------------------------------------------
// Wraps every public function body. bad_alloc is distinguished because callers
// can act on it; everything else collapses to KUI_ERR_INTERNAL because a C
// caller has no way to inspect a C++ exception anyway.
//
// Deliberately NOT guarding against SEH (access violations and the like). Once
// one of those fires the process state is already unreliable, so swallowing it
// would convert a loud crash into silent corruption. C++ exceptions are a
// different matter -- KUI throws them as part of normal operation, so they must
// be stopped at the boundary.
#define KUI_C_BEGIN try {
#define KUI_C_END                                   \
    }                                               \
    catch (const std::bad_alloc&) {                 \
        return KUI_ERR_OUT_OF_MEMORY;               \
    }                                               \
    catch (...) {                                   \
        return KUI_ERR_INTERNAL;                    \
    }

// --- 3. Strings ---------------------------------------------------------------
// Input: a view over the caller's buffer. Zero allocation, which is why the KUI
// methods taking StringView (getElementById, querySelector, hasAttribute,
// getAttribute) cost nothing extra through this layer. Methods declared as
// `const String&` still force one construction -- that is the C++ signature's
// doing, not this layer's.
inline std::string_view view(const char* p, size_t len) {
    if (!p) return {};
    return len ? std::string_view(p, len) : std::string_view(p);
}

// Same, when the target API insists on a std::string (costs one allocation).
inline std::string str(const char* p, size_t len) {
    const std::string_view v = view(p, len);
    return std::string(v);
}

// Output: the two-call pattern. Never allocates for the caller, so nothing
// crosses the CRT heap boundary.
//
//   buf == NULL / capacity 0 -> report the required length via out_len
//   capacity too small       -> truncate, NUL-terminate, KUI_ERR_BUFFER_TOO_SMALL,
//                               and out_len still reports the full length
//
// out_len always excludes the terminator; a NUL is always written whenever
// there is room for one.
inline KuiStatus writeString(std::string_view s, char* buf, size_t capacity,
                             size_t* out_len) {
    if (out_len) *out_len = s.size();

    if (!buf || capacity == 0) {
        // Length query. Without out_len there would be no observable effect,
        // so treat that as a caller mistake rather than a silent no-op.
        return out_len ? KUI_OK : KUI_ERR_INVALID_ARG;
    }

    if (s.size() + 1 > capacity) {
        if (!s.empty()) std::memcpy(buf, s.data(), capacity - 1);
        buf[capacity - 1] = '\0';
        return KUI_ERR_BUFFER_TOO_SMALL;
    }

    if (!s.empty()) std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    return KUI_OK;
}

}  // namespace kui_c
