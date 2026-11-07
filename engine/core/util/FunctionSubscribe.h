/*
 * Inspired by the work of http://nikitablack.github.io/post/std_function_as_delegate/
 */

// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef FUNCTIONSUBSCRIBE_H
#define FUNCTIONSUBSCRIBE_H

#include <cstdint>
#include <exception>
#include <functional>
#include <type_traits>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>
#include <algorithm>

#include "Log.h"
#include "LuaFunction.h"
#ifdef DORIAX_CRASH_GUARD
#include "util/CrashGuard.h"
#endif

#define DORIAX_EVENT_SUBSCRIPTION_TAG(METHOD) \
    (std::string(typeid(std::remove_pointer_t<decltype(this)>).name()) + \
     "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + \
     "_" #METHOD)

#define REGISTER_EVENT(EVENT_OBJ, METHOD) \
    do { \
        std::string __tag = DORIAX_EVENT_SUBSCRIPTION_TAG(METHOD); \
        (EVENT_OBJ).add< \
            std::remove_pointer_t<decltype(this)>, \
            &std::remove_pointer_t<decltype(this)>::METHOD \
        >(__tag, this); \
    } while (0)

#define UNREGISTER_EVENT(EVENT_OBJ, METHOD) \
    do { \
        std::string __tag = DORIAX_EVENT_SUBSCRIPTION_TAG(METHOD); \
        (EVENT_OBJ).remove(__tag); \
    } while (0)

#define REGISTER_ENGINE_EVENT(EVENT) \
    do { \
        std::string __tag = DORIAX_EVENT_SUBSCRIPTION_TAG(EVENT); \
        ::doriax::Engine::EVENT.add< \
            std::remove_pointer_t<decltype(this)>, \
            &std::remove_pointer_t<decltype(this)>::EVENT \
        >(__tag, this); \
    } while (0)

#define UNREGISTER_ENGINE_EVENT(EVENT) \
    UNREGISTER_EVENT(::doriax::Engine::EVENT, EVENT)

#define REGISTER_COMPONENT_EVENT(COMPONENT, EVENT, METHOD) \
    do { \
        auto* __scene = this->getScene(); \
        if (__scene) { \
            auto* __component = __scene->findComponent<COMPONENT>(this->getEntity()); \
            if (__component) { \
                std::string __tag = DORIAX_EVENT_SUBSCRIPTION_TAG(METHOD); \
                __component->EVENT.add< \
                    std::remove_pointer_t<decltype(this)>, \
                    &std::remove_pointer_t<decltype(this)>::METHOD \
                >(__tag, this); \
            } else { \
                ::doriax::Log::warn("Cannot register " #COMPONENT "::" #EVENT ": component not found"); \
            } \
        } \
    } while (0)

#define UNREGISTER_COMPONENT_EVENT(COMPONENT, EVENT, METHOD) \
    do { \
        auto* __scene = this->getScene(); \
        if (__scene) { \
            auto* __component = __scene->findComponent<COMPONENT>(this->getEntity()); \
            if (__component) { \
                UNREGISTER_EVENT(__component->EVENT, METHOD); \
            } \
        } \
    } while (0)

#define REGISTER_UI_EVENT(EVENT, METHOD) REGISTER_COMPONENT_EVENT(::doriax::UIComponent, EVENT, METHOD)
#define UNREGISTER_UI_EVENT(EVENT, METHOD) UNREGISTER_COMPONENT_EVENT(::doriax::UIComponent, EVENT, METHOD)

#define REGISTER_BUTTON_EVENT(EVENT, METHOD) REGISTER_COMPONENT_EVENT(::doriax::ButtonComponent, EVENT, METHOD)
#define UNREGISTER_BUTTON_EVENT(EVENT, METHOD) UNREGISTER_COMPONENT_EVENT(::doriax::ButtonComponent, EVENT, METHOD)

#define REGISTER_SCROLLBAR_EVENT(EVENT, METHOD) REGISTER_COMPONENT_EVENT(::doriax::ScrollbarComponent, EVENT, METHOD)
#define UNREGISTER_SCROLLBAR_EVENT(EVENT, METHOD) UNREGISTER_COMPONENT_EVENT(::doriax::ScrollbarComponent, EVENT, METHOD)

#define REGISTER_PANEL_EVENT(EVENT, METHOD) REGISTER_COMPONENT_EVENT(::doriax::PanelComponent, EVENT, METHOD)
#define UNREGISTER_PANEL_EVENT(EVENT, METHOD) UNREGISTER_COMPONENT_EVENT(::doriax::PanelComponent, EVENT, METHOD)


template<size_t>
struct MyPlaceholder {};

template<size_t N>
struct std::is_placeholder<MyPlaceholder<N>> : public std::integral_constant<size_t, N> {};

namespace doriax {

    #ifdef DORIAX_CRASH_GUARD
    using CrashHandler = std::function<void(const std::string& tag, const std::string& errorInfo)>;

    class FunctionSubscribeGlobal {
    public:
        static CrashHandler& getCrashHandler() {
            static CrashHandler handler = nullptr;
            return handler;
        }
    };

    // A C++ exception thrown by a guarded callback is reported like a crash
    template<typename Fn>
    inline void callCatching(std::string& exception, Fn&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            exception = std::string("Unhandled C++ exception (") + e.what() + ")";
        } catch (...) {
            exception = "Unhandled C++ exception";
        }
    }

    inline std::string crashDescription(const CrashInfo& ci) {
        return std::string(ci.name ? ci.name : "UNKNOWN") +
               " (" + (ci.description ? ci.description : "") +
               ") [code=" + std::to_string(ci.code) + "]";
    }
    #endif

    template<typename T>
    class FunctionSubscribe;

    template<typename Ret, typename ...Args>
    class FunctionSubscribe<Ret(Args...)> {

    private:
        // shared by copies, components are relocated by copying
        struct Subscriber {
            std::string tag;
            std::function<Ret(Args...)> function;
            int owners = 0;
            bool removed = false;
        };
        using SubscriberList = std::vector<std::shared_ptr<Subscriber>>;

        // copied on write, so a running call keeps its own
        std::shared_ptr<SubscriberList> subscribers;
        bool enabled = true;

        // a running call skips it once no list has it
        void release(const std::shared_ptr<Subscriber>& subscriber) {
            if (--subscriber->owners == 0) {
                subscriber->removed = true;
            }
        }

        SubscriberList& editableSubscribers() {
            if (!subscribers) {
                subscribers = std::make_shared<SubscriberList>();
            } else if (subscribers.use_count() > 1) {
                subscribers = std::make_shared<SubscriberList>(*subscribers);
            }
            return *subscribers;
        }

        // Helper to remove subscriber by index
        void removeAt(size_t index) {
            SubscriberList& list = editableSubscribers();
            release(list[index]);
            list.erase(list.begin() + index);
        }

        bool addImpl(const std::string& tag, std::function<Ret(Args...)> function){
            remove(tag);

            editableSubscribers().push_back(std::make_shared<Subscriber>(Subscriber{tag, std::move(function), 1}));

            return true;
        }

        void copyFrom(const FunctionSubscribe& t){
            if (t.subscribers) {
                for (const auto& subscriber : *t.subscribers) {
                    subscriber->owners++;
                }
            }
            clear();
            subscribers = t.subscribers;
            enabled = t.enabled;
        }

        template<typename T, size_t... Idx>
        std::function<Ret(Args...)> bindImpl(T *obj, Ret(T::*funcPtr)(Args...), std::index_sequence<Idx...>){
            return std::bind(funcPtr, obj, getPlaceholder<Idx>()...);
        }

        template<size_t N>
        MyPlaceholder<N + 1> getPlaceholder(){
            return {};
        }

    public:

        FunctionSubscribe() {
        }

        FunctionSubscribe(const FunctionSubscribe& t){
            copyFrom(t);
        }

        ~FunctionSubscribe() {
            clear();
        }

        FunctionSubscribe& operator = (const FunctionSubscribe& t){
            if (this != &t) {
                copyFrom(t);
            }

            return *this;
        }

        FunctionSubscribe(std::function<Ret(Args...)> function){
            add("cFunction", function);
        }

        FunctionSubscribe(lua_State *L) {
            add("luaFunction", L);
        }

        FunctionSubscribe& operator = (std::function<Ret(Args...)> function){
            add("cFunction", function);

            return *this;
        }

        FunctionSubscribe& operator = (lua_State *L){
            add("luaFunction", L);

            return *this;
        }

        void setEnabled(bool enabled) {
            this->enabled = enabled;
        }

        bool isEnabled() const {
            return enabled;
        }

        bool add(const std::string& tag, lua_State *L){
            std::function<Ret(Args...)> function = LuaFunction<Ret>(L);
            addImpl(tag, function);
            return true;
        }

        bool add(const std::string& tag, std::function<Ret(Args...)> function) {
            addImpl(tag, function);
            return true;
        }

        template<Ret(*funcPtr)(Args...)>
        bool add(const std::string& tag){
            addImpl(tag, std::function<Ret(Args...)>(funcPtr));
            return true;
        }

        template<typename T, Ret(T::*funcPtr)(Args...)>
        bool add(const std::string& tag, std::shared_ptr<T> obj){
            addImpl(tag, bindImpl(obj.get(), funcPtr, std::index_sequence_for<Args...>{}));
            return true;
        }

        template<typename T, Ret(T::*funcPtr)(Args...)>
        bool add(const std::string& tag, T* obj){
            addImpl(tag, bindImpl(obj, funcPtr, std::index_sequence_for<Args...>{}));
            return true;
        }

        template<typename T>
        bool add(const std::string& tag, std::shared_ptr<T> t){
            addImpl(tag, *t.get());
            return true;
        }

        bool remove(const std::string& tag){
            for (size_t i = 0; subscribers && i < subscribers->size(); i++) {
                if ((*subscribers)[i]->tag == tag) {
                    removeAt(i);
                    return true;
                }
            }

            return false;
        }

        size_t removeByTagSubstring(const std::string& substring){
            size_t removed = 0;
            for (size_t i = 0; subscribers && i < subscribers->size(); ) {
                if ((*subscribers)[i]->tag.find(substring) != std::string::npos) {
                    removeAt(i);
                    ++removed;
                } else {
                    ++i;
                }
            }
            return removed;
        }

        Ret call(Args... args){
            if (!enabled){
                if constexpr (!std::is_void<Ret>::value) {
                    return Ret();
                }
            }else{
                if constexpr (std::is_void<Ret>::value) {
                    // a callback can change or destroy this object
                    auto list = subscribers;
                    if (!list) return;
                    for (const auto& subscriber : *list) {
                        if (subscriber->removed) continue;
                        auto& function = subscriber->function;
                        #ifdef DORIAX_CRASH_GUARD
                        // Use crash protection if handler is registered
                        auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
                        if (crashHandler) {
                            CrashInfo ci{};
                            std::string exception;
                            bool ok = callWithCrashGuard([&](){
                                callCatching(exception, [&](){ function(args...); });
                            }, &ci);

                            if (!ok || !exception.empty()) {
                                crashHandler(subscriber->tag, ok ? exception : crashDescription(ci));

                                subscriber->removed = true;
                            }
                        } else {
                            function(args...);
                        }
                        #else
                        function(args...);
                        #endif
                    }
                } else {
                    return callRet(args..., Ret());
                }
            }
        }

        template<typename T>
        Ret callRet(Args... args, T def){
            if (!enabled){
                return def;
            }
            // a callback can change or destroy this object
            auto list = subscribers;
            if (!list){
                return def;
            }
            for (const auto& subscriber : *list) {
                if (subscriber->removed) continue;
                auto& function = subscriber->function;
                #ifdef DORIAX_CRASH_GUARD
                // Use crash protection if handler is registered
                auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
                if (crashHandler) {
                    Ret result{};
                    CrashInfo ci{};
                    std::string exception;
                    bool ok = callWithCrashGuard([&](){
                        callCatching(exception, [&](){ result = function(args...); });
                    }, &ci);

                    if (ok && exception.empty()) {
                        return result;
                    } else {
                        crashHandler(subscriber->tag, ok ? exception : crashDescription(ci));

                        subscriber->removed = true;
                    }
                } else {
                    return function(args...);
                }
                #else
                return function(args...);
                #endif
            }
            return def;
        }

        Ret operator()(Args... args){
            return call(args...);
        }

        bool empty() const {
            return !subscribers || subscribers->empty();
        }

        void clear(){
            if (subscribers) {
                for (const auto& subscriber : *subscribers) {
                    release(subscriber);
                }
                subscribers.reset();
            }
        }
    };
}

#endif //FUNCTIONSUBSCRIBE_H