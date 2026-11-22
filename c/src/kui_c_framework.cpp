// =============================================================================
// kui_c_framework.cpp — section 11 of kui_c.h: router, data binding, animation
// state and Mutation / Resize / Intersection observers (3.61.8).
//
// Lifetime rules shared by everything here:
//   * A callback's user_data lives in a UserData holder whose destructor calls
//     release_fn. Every native std::function that needs it captures the same
//     shared_ptr, so release_fn fires exactly once, after the last copy is gone.
//     When a call fails the holder is disarmed first: user_data stays the
//     caller's and release_fn is not called.
//   * KuiObservable / KuiObserver are C-side objects with their own atomic
//     count. If the last reference is dropped while one of their own callbacks
//     runs, the object is detached at once (unbound / unregistered, so nothing
//     calls back again) but freed only after the native code that called back
//     has returned -- through the main-thread queue, or never when the platform
//     is not running (a leak is better than freeing memory still on the stack).
//   * The native BindingManager and observer registries keep raw pointers. The
//     C objects therefore hold references to every element and document they
//     are attached to, and detach before the native objects go away.
// =============================================================================
#include "kui_c_internal.h"

#include <kui/animation/animation_manager.h>
#include <kui/animation/transition.h>
#include <kui/data/binding_manager.h>
#include <kui/data/observable.h>
#include <kui/dom/document.h>
#include <kui/dom/element.h>
#include <kui/dom/intersection_observer.h>
#include <kui/dom/mutation_observer.h>
#include <kui/dom/node.h>
#include <kui/dom/resize_observer.h>
#include <kui/router/router.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using kui_c::ArmOnSuccess;
using kui_c::CallbackScope;
using kui_c::fromHandle;
using kui_c::str;
using kui_c::toHandle;
using kui_c::UserData;
using kui_c::UserDataPtr;
using kui_c::writeString;

namespace {

inline kui::Document* doc(KuiDocument* value) { return fromHandle<kui::Document>(value); }
inline kui::Element* elem(KuiElement* value) { return fromHandle<kui::Element>(value); }

KuiElement* asElement(kui::Node* node) {
    return node && node->isElement() ? toHandle<KuiElement>(static_cast<kui::Element*>(node)) : nullptr;
}
int nodeType(const kui::Node* node) { return node ? static_cast<int>(node->nodeType()) : 0; }

// --- Router ------------------------------------------------------------------
struct RouteInfoView {
    std::vector<KuiCKeyValue> params;
    std::vector<KuiCKeyValue> query;
    KuiCRouteInfo info{};

    explicit RouteInfoView(const kui::RouteContext& route) {
        fill(route.params, params);
        fill(route.queryParams, query);
        info.path = route.path.c_str();
        info.path_len = route.path.size();
        info.pattern = route.pattern.c_str();
        info.pattern_len = route.pattern.size();
        info.query = route.query.c_str();
        info.query_len = route.query.size();
        info.params = params.empty() ? nullptr : params.data();
        info.param_count = params.size();
        info.query_params = query.empty() ? nullptr : query.data();
        info.query_param_count = query.size();
    }

    static void fill(const std::unordered_map<kui::String, kui::String>& map,
                     std::vector<KuiCKeyValue>& out) {
        out.reserve(map.size());
        for (const auto& entry : map) {
            out.push_back({entry.first.c_str(), entry.first.size(), entry.second.c_str(),
                           entry.second.size()});
        }
    }
};

// Router::registerView keeps a raw Element*. The C layer holds a reference to
// each registered view so it cannot dangle; the holder is captured by a no-op
// after-navigate callback, so the Router itself owns it and it goes away with
// the document.
struct ViewKeeper {
    std::vector<kui::RefPtr<kui::Element>> views;
};

std::shared_ptr<ViewKeeper> viewKeeper(kui::Document* document, bool create) {
    static std::unordered_map<kui::Document*, std::weak_ptr<ViewKeeper>> keepers;
    for (auto it = keepers.begin(); it != keepers.end();) {
        it = it->second.expired() ? keepers.erase(it) : std::next(it);
    }
    auto found = keepers.find(document);
    if (found != keepers.end()) return found->second.lock();
    if (!create) return nullptr;
    auto keeper = std::make_shared<ViewKeeper>();
    document->router().onAfterNavigate([keeper](const kui::RouteContext&) {});
    keepers.emplace(document, keeper);
    return keeper;
}

template <typename T>
void releaseObject(T* object) {
    if (object->refs.fetch_sub(1) != 1) return;
    if (object->callbackDepth > 0) {
        object->detach();
        object->releasedInCallback = true;  // CallbackScope frees it on the way out
        return;
    }
    delete object;
}

}  // namespace

// --- C-side objects (complete the opaque types of kui_c.h) ---------------------
struct KuiObservable {
    std::atomic<int> refs{1};
    int callbackDepth = 0;
    bool releasedInCallback = false;
    kui::Observable<kui::String> value;
    std::vector<kui::SubscriptionId> subscriptions;
    struct Binding {
        kui::RefPtr<kui::Document> document;
        kui::RefPtr<kui::Element> element;
        kui::BindingId native = 0;
    };
    std::unordered_map<uint32_t, Binding> bindings;
    uint32_t nextBinding = 1;

    void unbindAll() {
        auto drained = std::move(bindings);
        bindings.clear();
        for (auto& entry : drained) entry.second.document->bindingManager().unbind(entry.second.native);
    }
    void detach() {
        unbindAll();
        auto drained = std::move(subscriptions);
        subscriptions.clear();
        for (kui::SubscriptionId id : drained) value.unsubscribe(id);
    }
    ~KuiObservable() { detach(); }
};

struct KuiObserver {
    enum class Kind { Mutation, Resize, Intersection };
    explicit KuiObserver(Kind value) : kind(value) {}

    Kind kind;
    std::atomic<int> refs{1};
    int callbackDepth = 0;
    bool releasedInCallback = false;
    UserDataPtr user;
    KuiMutationFn mutationFn = nullptr;
    KuiResizeFn resizeFn = nullptr;
    KuiIntersectionFn intersectionFn = nullptr;
    kui::RefPtr<kui::MutationObserver> mutation;
    kui::RefPtr<kui::ResizeObserver> resize;
    kui::RefPtr<kui::IntersectionObserver> intersection;
    kui::RefPtr<kui::Element> root;
    std::vector<kui::RefPtr<kui::Element>> targets;
    std::vector<kui::RefPtr<kui::Document>> documents;  // whose registry we are in

    void registerWith(kui::Document* document) {
        for (const auto& existing : documents) {
            if (existing.get() == document) return;
        }
        documents.emplace_back(document);
        switch (kind) {
        case Kind::Mutation: document->mutationObserverRegistry().registerObserver(mutation.get()); break;
        case Kind::Resize: document->resizeObserverRegistry().registerObserver(resize.get()); break;
        case Kind::Intersection:
            document->intersectionObserverRegistry().registerObserver(intersection.get());
            break;
        }
    }
    void keepTarget(kui::Element* target) {
        for (const auto& existing : targets) {
            if (existing.get() == target) return;
        }
        targets.emplace_back(target);
    }
    void disconnect() {
        auto drained = std::move(documents);
        documents.clear();
        for (const auto& document : drained) {
            switch (kind) {
            case Kind::Mutation: document->mutationObserverRegistry().unregisterObserver(mutation.get()); break;
            case Kind::Resize: document->resizeObserverRegistry().unregisterObserver(resize.get()); break;
            case Kind::Intersection:
                document->intersectionObserverRegistry().unregisterObserver(intersection.get());
                break;
            }
        }
        if (mutation) mutation->disconnect();
        if (resize) resize->disconnect();
        if (intersection) intersection->disconnect();
        targets.clear();
    }
    void detach() { disconnect(); }
    ~KuiObserver() { disconnect(); }
};

namespace {

KuiRect toRect(const kui::Rect& rect) { return KuiRect{rect.x, rect.y, rect.width, rect.height}; }

KuiCMutationRecord toRecord(const kui::MutationRecord& record) {
    KuiCMutationRecord out{};
    switch (record.type) {
    case kui::MutationRecord::Type::ChildList: out.type = KUI_C_MUTATION_CHILD_LIST; break;
    case kui::MutationRecord::Type::Attributes: out.type = KUI_C_MUTATION_ATTRIBUTES; break;
    case kui::MutationRecord::Type::CharacterData: out.type = KUI_C_MUTATION_CHARACTER_DATA; break;
    }
    kui::Node* target = record.target.get();
    out.target_node_type = nodeType(target);
    out.target = target && !target->isElement() ? asElement(target->parentNode()) : asElement(target);
    out.added = asElement(record.addedNode.get());
    out.added_node_type = nodeType(record.addedNode.get());
    out.removed = asElement(record.removedNode.get());
    out.removed_node_type = nodeType(record.removedNode.get());
    out.previous_sibling = asElement(record.previousSibling.get());
    out.next_sibling = asElement(record.nextSibling.get());
    out.attribute_name = record.attributeName.c_str();
    out.attribute_name_len = record.attributeName.size();
    out.old_value = record.oldValue.c_str();
    out.old_value_len = record.oldValue.size();
    return out;
}

// Observing needs a laid-out frame to report the first size / intersection; an
// idle page would otherwise wait for an unrelated layout change.
void requestLayout(kui::Element* target, kui::Document* document) {
    target->markNeedsLayout();
    document->scheduleRepaint();
}

}  // namespace

extern "C" {

// --- Router ------------------------------------------------------------------
KuiStatus kui_document_router_add_route(KuiDocument* document, const char* pattern,
                                                   size_t pattern_len, const char* name,
                                                   size_t name_len, KuiRouteViewFn fn,
                                                   void* user_data, KuiTaskFn release_fn) {
    KUI_C_BEGIN
    if (!document || !pattern || !fn) return KUI_ERR_INVALID_ARG;
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    kui::RouteDefinition def;
    def.path = str(pattern, pattern_len);
    if (name) def.name = str(name, name_len);
    UserDataPtr holder = guard.holder;
    def.factory = [holder, fn](kui::Document* owner, const kui::RouteParams&) -> kui::RefPtr<kui::Node> {
        RouteInfoView view(owner->router().currentRoute());
        KuiElement* made = fn(toHandle<KuiDocument>(owner), &view.info, holder->data);
        if (!made) return nullptr;
        // The callback handed over a counted reference: adopt it, do not add one.
        return kui::RefPtr<kui::Node>(kui::adoptRef(elem(made)));
    };
    if (def.path.empty() || !doc(document)->router().addRoute(std::move(def))) return KUI_ERR_INVALID_ARG;
    guard.commit();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_remove_route(KuiDocument* document, const char* pattern,
                                                      size_t len) {
    KUI_C_BEGIN
    if (!document || !pattern) return KUI_ERR_INVALID_ARG;
    return doc(document)->router().removeRoute(str(pattern, len)) ? KUI_OK : KUI_ERR_NOT_FOUND;
    KUI_C_END
}

KuiStatus kui_document_router_clear_routes(KuiDocument* document) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    doc(document)->router().clearRoutes();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_register_view(KuiDocument* document, KuiElement* el) {
    KUI_C_BEGIN
    if (!document || !el || elem(el)->ownerDocument() != doc(document)) return KUI_ERR_INVALID_ARG;
    auto keeper = viewKeeper(doc(document), true);
    kui::Element* view = elem(el);
    const bool known = std::any_of(keeper->views.begin(), keeper->views.end(),
                                   [view](const kui::RefPtr<kui::Element>& kept) {
                                       return kept.get() == view;
                                   });
    if (!known) keeper->views.emplace_back(view);
    doc(document)->router().registerView(view);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_unregister_view(KuiDocument* document, KuiElement* el) {
    KUI_C_BEGIN
    if (!document || !el) return KUI_ERR_INVALID_ARG;
    auto keeper = viewKeeper(doc(document), false);
    kui::Element* view = elem(el);
    if (!keeper) return KUI_ERR_NOT_FOUND;
    auto found = std::find_if(keeper->views.begin(), keeper->views.end(),
                              [view](const kui::RefPtr<kui::Element>& kept) {
                                  return kept.get() == view;
                              });
    if (found == keeper->views.end()) return KUI_ERR_NOT_FOUND;
    doc(document)->router().unregisterView(view);
    keeper->views.erase(found);  // after the router dropped its raw pointer
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_has_route(KuiDocument* document, const char* pattern,
                                                   size_t len, int* out_has) {
    KUI_C_BEGIN
    if (!document || !pattern || !out_has) return KUI_ERR_INVALID_ARG;
    *out_has = doc(document)->router().hasRoute(str(pattern, len)) ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_get_route_count(KuiDocument* document, uint32_t* out_count) {
    KUI_C_BEGIN
    if (!document || !out_count) return KUI_ERR_INVALID_ARG;
    *out_count = doc(document)->router().routeCount();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_push(KuiDocument* document, const char* path, size_t len,
                                              int* out_navigated) {
    KUI_C_BEGIN
    if (!document || !path) return KUI_ERR_INVALID_ARG;
    const bool navigated = doc(document)->router().push(str(path, len));
    if (out_navigated) *out_navigated = navigated ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_replace(KuiDocument* document, const char* path, size_t len,
                                                 int* out_navigated) {
    KUI_C_BEGIN
    if (!document || !path) return KUI_ERR_INVALID_ARG;
    const bool navigated = doc(document)->router().replace(str(path, len));
    if (out_navigated) *out_navigated = navigated ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_go(KuiDocument* document, int delta, int* out_navigated) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    const bool navigated = doc(document)->router().go(delta);
    if (out_navigated) *out_navigated = navigated ? 1 : 0;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_get_current(KuiDocument* document, KuiRouteInfoFn fn,
                                                     void* user_data) {
    KUI_C_BEGIN
    if (!document || !fn) return KUI_ERR_INVALID_ARG;
    RouteInfoView view(doc(document)->router().currentRoute());
    fn(&view.info, user_data);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_get_current_path(KuiDocument* document, char* buf,
                                                          size_t buf_capacity, size_t* out_len) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    return writeString(doc(document)->router().currentPath(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_document_router_get_param(KuiDocument* document, const char* name,
                                                   size_t name_len, char* buf,
                                                   size_t buf_capacity, size_t* out_len) {
    KUI_C_BEGIN
    if (!document || !name) return KUI_ERR_INVALID_ARG;
    const auto& params = doc(document)->router().currentParams();
    const auto found = params.find(str(name, name_len));
    if (found == params.end()) return KUI_ERR_NOT_FOUND;
    return writeString(found->second, buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_document_router_get_history(KuiDocument* document, int* out_index,
                                                     int* out_size) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    const kui::Router& router = doc(document)->router();
    if (out_index) *out_index = router.historyIndex();
    if (out_size) *out_size = router.historySize();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_add_guard(KuiDocument* document, KuiCRouteGuardKind kind,
                                                   KuiRouteGuardFn fn, void* user_data,
                                                   KuiTaskFn release_fn, uint32_t* out_id) {
    KUI_C_BEGIN
    if (!document || !fn || !out_id) return KUI_ERR_INVALID_ARG;
    if (kind != KUI_C_ROUTE_GUARD_BEFORE_ENTER && kind != KUI_C_ROUTE_GUARD_BEFORE_LEAVE) {
        return KUI_ERR_INVALID_ARG;
    }
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    UserDataPtr holder = guard.holder;
    kui::NavigationGuard native = [holder, fn](const kui::RouteContext& from,
                                               const kui::RouteContext& to) {
        RouteInfoView fromView(from);
        RouteInfoView toView(to);
        return fn(&fromView.info, &toView.info, holder->data) != 0;
    };
    kui::Router& router = doc(document)->router();
    *out_id = kind == KUI_C_ROUTE_GUARD_BEFORE_ENTER ? router.addBeforeEnter(std::move(native))
                                                     : router.addBeforeLeave(std::move(native));
    guard.commit();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_remove_guard(KuiDocument* document, uint32_t id) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    doc(document)->router().removeGuard(id);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_add_navigated_callback(KuiDocument* document,
                                                                KuiRouteInfoFn fn,
                                                                void* user_data,
                                                                KuiTaskFn release_fn,
                                                                uint32_t* out_id) {
    KUI_C_BEGIN
    if (!document || !fn || !out_id) return KUI_ERR_INVALID_ARG;
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    UserDataPtr holder = guard.holder;
    *out_id = doc(document)->router().onAfterNavigate([holder, fn](const kui::RouteContext& route) {
        RouteInfoView view(route);
        fn(&view.info, holder->data);
    });
    guard.commit();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_router_remove_navigated_callback(KuiDocument* document,
                                                                   uint32_t id) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    doc(document)->router().removeAfterNavigate(id);
    return KUI_OK;
    KUI_C_END
}

// --- Data binding --------------------------------------------------------------
KuiStatus kui_observable_create(const char* initial, size_t len, KuiObservable** out) {
    KUI_C_BEGIN
    if (!out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    auto* obs = new KuiObservable();
    if (initial) obs->value.set(str(initial, len));
    *out = obs;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_retain(KuiObservable* obs) {
    if (!obs) return KUI_ERR_INVALID_ARG;
    obs->refs.fetch_add(1);
    return KUI_OK;
}

KuiStatus kui_observable_release(KuiObservable* obs) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    releaseObject(obs);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_set(KuiObservable* obs, const char* value, size_t len) {
    KUI_C_BEGIN
    if (!obs || !value) return KUI_ERR_INVALID_ARG;
    // A subscriber may drop the caller's last reference; keep the object alive
    // until notify() has fully returned.
    obs->refs.fetch_add(1);
    struct Hold {
        KuiObservable* object;
        ~Hold() { releaseObject(object); }
    } hold{obs};
    obs->value.set(str(value, len));
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_get(KuiObservable* obs, char* buf, size_t buf_capacity,
                                        size_t* out_len) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    return writeString(obs->value.get(), buf, buf_capacity, out_len);
    KUI_C_END
}

KuiStatus kui_observable_subscribe(KuiObservable* obs, KuiTaskFn fn, void* user_data,
                                              KuiTaskFn release_fn, uint32_t* out_id) {
    KUI_C_BEGIN
    if (!obs || !fn || !out_id) return KUI_ERR_INVALID_ARG;
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    UserDataPtr holder = guard.holder;
    KuiObservable* self = obs;
    const kui::SubscriptionId id = obs->value.subscribe([self, holder, fn]() {
        UserDataPtr keep = holder;  // release_fn waits until fn has returned
        CallbackScope<KuiObservable> scope(self);
        fn(keep->data);
    });
    obs->subscriptions.push_back(id);
    *out_id = id;
    guard.commit();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_unsubscribe(KuiObservable* obs, uint32_t id) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    // Only ids handed out here: binding subscriptions share the id space.
    auto found = std::find(obs->subscriptions.begin(), obs->subscriptions.end(), id);
    if (found == obs->subscriptions.end()) return KUI_ERR_NOT_FOUND;
    obs->subscriptions.erase(found);
    obs->value.unsubscribe(id);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_bind(KuiObservable* obs, KuiElement* el, const char* property,
                                         size_t property_len, int two_way,
                                         uint32_t* out_binding_id) {
    KUI_C_BEGIN
    if (!obs || !el || !property) return KUI_ERR_INVALID_ARG;
    kui::Element* element = elem(el);
    kui::Document* owner = element->ownerDocument();
    const std::string name = str(property, property_len);
    if (!owner || name.empty()) return KUI_ERR_INVALID_ARG;
    kui::BindingManager& manager = owner->bindingManager();
    const kui::BindingId native = two_way ? manager.bindTwoWay(element, name, &obs->value)
                                          : manager.bind(element, name, &obs->value);
    if (!native) return KUI_ERR_INVALID_ARG;
    const uint32_t id = obs->nextBinding++;
    obs->bindings.emplace(id, KuiObservable::Binding{kui::RefPtr<kui::Document>(owner),
                                                     kui::RefPtr<kui::Element>(element), native});
    if (out_binding_id) *out_binding_id = id;
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_unbind(KuiObservable* obs, uint32_t binding_id) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    auto found = obs->bindings.find(binding_id);
    if (found == obs->bindings.end()) return KUI_ERR_NOT_FOUND;
    KuiObservable::Binding binding = std::move(found->second);
    obs->bindings.erase(found);
    binding.document->bindingManager().unbind(binding.native);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observable_unbind_all(KuiObservable* obs) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    obs->unbindAll();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_flush_bindings(KuiDocument* document) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    doc(document)->bindingManager().flush();
    return KUI_OK;
    KUI_C_END
}

// --- Animation state -----------------------------------------------------------
KuiStatus kui_element_get_animation_state(KuiElement* el, int* out_animating,
                                                     int* out_transitioning) {
    KUI_C_BEGIN
    if (!el) return KUI_ERR_INVALID_ARG;
    kui::Element* element = elem(el);
    kui::Document* owner = element->ownerDocument();
    if (out_animating) {
        *out_animating = owner && owner->animationManager()->hasActiveAnimations(element) ? 1 : 0;
    }
    if (out_transitioning) {
        *out_transitioning =
            owner && owner->transitionManager()->hasActiveTransitions(element) ? 1 : 0;
    }
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_document_get_animation_counts(KuiDocument* document,
                                                       uint32_t* out_animations,
                                                       uint32_t* out_transitions) {
    KUI_C_BEGIN
    if (!document) return KUI_ERR_INVALID_ARG;
    if (out_animations) *out_animations = doc(document)->animationManager()->activeAnimationCount();
    if (out_transitions) {
        *out_transitions = doc(document)->transitionManager()->activeTransitionCount();
    }
    return KUI_OK;
    KUI_C_END
}

// --- Observers -----------------------------------------------------------------
KuiStatus kui_mutation_observer_create(KuiMutationFn fn, void* user_data,
                                                  KuiTaskFn release_fn, KuiObserver** out) {
    KUI_C_BEGIN
    if (!fn || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    auto observer = std::make_unique<KuiObserver>(KuiObserver::Kind::Mutation);
    KuiObserver* self = observer.get();
    observer->user = guard.holder;
    observer->mutationFn = fn;
    observer->mutation = kui::MutationObserver::create(
        [self](const std::vector<kui::MutationRecord>& records, kui::MutationObserver*) {
            if (records.empty()) return;
            std::vector<KuiCMutationRecord> out;
            out.reserve(records.size());
            for (const auto& record : records) out.push_back(toRecord(record));
            CallbackScope<KuiObserver> scope(self);
            self->mutationFn(out.data(), out.size(), self->user->data);
        });
    guard.commit();
    *out = observer.release();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_resize_observer_create(KuiResizeFn fn, void* user_data,
                                                KuiTaskFn release_fn, KuiObserver** out) {
    KUI_C_BEGIN
    if (!fn || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    auto observer = std::make_unique<KuiObserver>(KuiObserver::Kind::Resize);
    KuiObserver* self = observer.get();
    observer->user = guard.holder;
    observer->resizeFn = fn;
    observer->resize = kui::ResizeObserver::create(
        [self](const std::vector<kui::ResizeObserverEntry>& entries, kui::ResizeObserver*) {
            if (entries.empty()) return;
            std::vector<KuiCResizeEntry> out;
            out.reserve(entries.size());
            for (const auto& entry : entries) {
                KuiCResizeEntry item{};
                item.target = asElement(entry.target);
                item.content_width = entry.contentBoxSize.inlineSize;
                item.content_height = entry.contentBoxSize.blockSize;
                item.border_width = entry.borderBoxSize.inlineSize;
                item.border_height = entry.borderBoxSize.blockSize;
                item.content_x = entry.contentRectX;
                item.content_y = entry.contentRectY;
                out.push_back(item);
            }
            CallbackScope<KuiObserver> scope(self);
            self->resizeFn(out.data(), out.size(), self->user->data);
        });
    guard.commit();
    *out = observer.release();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_intersection_observer_create(const KuiCIntersectionOptions* options,
                                                      KuiIntersectionFn fn, void* user_data,
                                                      KuiTaskFn release_fn, KuiObserver** out) {
    KUI_C_BEGIN
    if (!fn || !out) return KUI_ERR_INVALID_ARG;
    *out = nullptr;
    kui::IntersectionObserverInit init;
    kui::RefPtr<kui::Element> root;
    if (options) {
        if (options->threshold_count && !options->thresholds) return KUI_ERR_INVALID_ARG;
        if (options->threshold_count) {
            init.thresholds.clear();
            for (size_t index = 0; index < options->threshold_count; ++index) {
                const float value = options->thresholds[index];
                if (!(value >= 0.0f && value <= 1.0f)) return KUI_ERR_INVALID_ARG;
                init.thresholds.push_back(value);
            }
            std::sort(init.thresholds.begin(), init.thresholds.end());
        }
        init.rootMargin.top = options->margin_top;
        init.rootMargin.right = options->margin_right;
        init.rootMargin.bottom = options->margin_bottom;
        init.rootMargin.left = options->margin_left;
        if (options->root) {
            root = kui::RefPtr<kui::Element>(elem(options->root));
            init.root = root.get();
        }
    }
    ArmOnSuccess guard(std::make_shared<UserData>(user_data, release_fn));
    auto observer = std::make_unique<KuiObserver>(KuiObserver::Kind::Intersection);
    KuiObserver* self = observer.get();
    observer->user = guard.holder;
    observer->intersectionFn = fn;
    observer->root = std::move(root);
    observer->intersection = kui::IntersectionObserver::create(
        [self](const std::vector<kui::IntersectionObserverEntry>& entries, kui::IntersectionObserver*) {
            if (entries.empty()) return;
            std::vector<KuiCIntersectionEntry> out;
            out.reserve(entries.size());
            for (const auto& entry : entries) {
                KuiCIntersectionEntry item{};
                item.target = asElement(entry.target);
                item.bounding_client_rect = toRect(entry.boundingClientRect);
                item.intersection_rect = toRect(entry.intersectionRect);
                item.root_bounds = toRect(entry.rootBounds);
                item.intersection_ratio = entry.intersectionRatio;
                item.is_intersecting = entry.isIntersecting ? 1 : 0;
                out.push_back(item);
            }
            CallbackScope<KuiObserver> scope(self);
            self->intersectionFn(out.data(), out.size(), self->user->data);
        },
        init);
    guard.commit();
    *out = observer.release();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_mutation_observer_observe(KuiObserver* obs, KuiElement* target,
                                                   const KuiCMutationOptions* options) {
    KUI_C_BEGIN
    if (!obs || !target || !options || obs->kind != KuiObserver::Kind::Mutation) {
        return KUI_ERR_INVALID_ARG;
    }
    if (!options->child_list && !options->attributes && !options->character_data) {
        return KUI_ERR_INVALID_ARG;
    }
    if (options->attribute_filter_count && !options->attribute_filter) return KUI_ERR_INVALID_ARG;
    kui::MutationObserverInit init;
    init.childList = options->child_list != 0;
    init.attributes = options->attributes != 0;
    init.characterData = options->character_data != 0;
    init.subtree = options->subtree != 0;
    init.attributeOldValue = options->attribute_old_value != 0;
    init.characterDataOldValue = options->character_data_old_value != 0;
    for (size_t index = 0; index < options->attribute_filter_count; ++index) {
        const char* name = options->attribute_filter[index];
        if (!name || !*name) return KUI_ERR_INVALID_ARG;
        init.attributeFilter.emplace_back(name);
    }
    kui::Element* element = elem(target);
    kui::Document* owner = element->ownerDocument();
    if (!owner) return KUI_ERR_INVALID_ARG;
    obs->mutation->observe(element, init);
    obs->keepTarget(element);
    obs->registerWith(owner);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observer_observe(KuiObserver* obs, KuiElement* target) {
    KUI_C_BEGIN
    if (!obs || !target || obs->kind == KuiObserver::Kind::Mutation) return KUI_ERR_INVALID_ARG;
    kui::Element* element = elem(target);
    kui::Document* owner = element->ownerDocument();
    if (!owner) return KUI_ERR_INVALID_ARG;
    if (obs->kind == KuiObserver::Kind::Resize) {
        obs->resize->observe(element);
    } else {
        obs->intersection->observe(element);
    }
    obs->keepTarget(element);
    obs->registerWith(owner);
    requestLayout(element, owner);
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observer_unobserve(KuiObserver* obs, KuiElement* target) {
    KUI_C_BEGIN
    if (!obs || !target) return KUI_ERR_INVALID_ARG;
    if (obs->kind == KuiObserver::Kind::Mutation) return KUI_ERR_NOT_SUPPORTED;
    kui::Element* element = elem(target);
    if (obs->kind == KuiObserver::Kind::Resize) {
        obs->resize->unobserve(element);
    } else {
        obs->intersection->unobserve(element);
    }
    obs->targets.erase(std::remove_if(obs->targets.begin(), obs->targets.end(),
                                      [element](const kui::RefPtr<kui::Element>& kept) {
                                          return kept.get() == element;
                                      }),
                       obs->targets.end());
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observer_disconnect(KuiObserver* obs) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    obs->disconnect();
    return KUI_OK;
    KUI_C_END
}

KuiStatus kui_observer_retain(KuiObserver* obs) {
    if (!obs) return KUI_ERR_INVALID_ARG;
    obs->refs.fetch_add(1);
    return KUI_OK;
}

KuiStatus kui_observer_release(KuiObserver* obs) {
    KUI_C_BEGIN
    if (!obs) return KUI_ERR_INVALID_ARG;
    releaseObject(obs);
    return KUI_OK;
    KUI_C_END
}

}  // extern "C"
