/*
 * Inspired by the work of http://nikitablack.github.io/post/std_function_as_delegate/
 */

//
// (c) 2026 Eduardo Doria.
//

#ifndef FUNCTIONSUBSCRIBE_H
#define FUNCTIONSUBSCRIBE_H

#include <functional>
#include <type_traits>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "LuaFunction.h"
#ifdef DORIAX_CRASH_GUARD
#include "util/CrashGuard.h"
#endif

#define REGISTER_EVENT(EVENT_OBJ, METHOD) \
    do { \
        std::string __tag = std::string(typeid(std::remove_pointer_t<decltype(this)>).name()) + \
                            "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + \
                            "_" #METHOD; \
        (EVENT_OBJ).add< \
            std::remove_pointer_t<decltype(this)>, \
            &std::remove_pointer_t<decltype(this)>::METHOD \
        >(__tag, this); \
    } while (0)

#define REGISTER_ENGINE_EVENT(EVENT) \
    do { \
        std::string __tag = std::string(typeid(std::remove_pointer_t<decltype(this)>).name()) + \
                            "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + \
                            "_" #EVENT; \
        ::doriax::Engine::EVENT.add< \
            std::remove_pointer_t<decltype(this)>, \
            &std::remove_pointer_t<decltype(this)>::EVENT \
        >(__tag, this); \
    } while (0)


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
    #endif

    template<typename T>
    class FunctionSubscribe;

    template<typename Ret, typename ...Args>
    class FunctionSubscribe<Ret(Args...)> {

    private:
        std::vector<std::function<Ret(Args...)>> functions;
        std::vector<std::string> tags;
        bool enabled = true;

        // Helper to remove subscriber by index
        void removeAt(size_t index) {
            functions.erase(functions.begin() + index);
            tags.erase(tags.begin() + index);
        }

        bool addImpl(const std::string& tag, std::function<Ret(Args...)> function){
            if (find(tags.begin(), tags.end(), tag) != tags.end())
            {
                remove(tag);
            }

            functions.push_back(function);
            tags.push_back(tag);

            return true;
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
            this->functions = t.functions;
            this->tags = t.tags;
            this->enabled = t.enabled;
        }

        FunctionSubscribe& operator = (const FunctionSubscribe& t){
            this->functions = t.functions;
            this->tags = t.tags;
            this->enabled = t.enabled;

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
            auto it = find(tags.begin(), tags.end(), tag);
            if (it == tags.end()){
                return false;
            }

            auto index{ std::distance(tags.begin(), it) };
            tags.erase(it);

            functions.erase(functions.begin() + index);

            return true;
        }

        size_t removeByTagSubstring(const std::string& substring){
            size_t removed = 0;
            for (size_t i = 0; i < tags.size(); ) {
                if (tags[i].find(substring) != std::string::npos) {
                    tags.erase(tags.begin() + i);
                    functions.erase(functions.begin() + i);
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
                    for (size_t i = 0; i < functions.size(); ) {
                        auto& function = functions[i];
                        #ifdef DORIAX_CRASH_GUARD
                        // Use crash protection if handler is registered
                        auto& tag = tags[i];
                        auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
                        if (crashHandler) {
                            CrashInfo ci{};
                            bool ok = callWithCrashGuard([&](){
                                function(args...);
                            }, &ci);

                            if (!ok) {
                                std::string errorInfo = std::string(ci.name ? ci.name : "UNKNOWN") + 
                                                    " (" + (ci.description ? ci.description : "") + 
                                                    ") [code=" + std::to_string(ci.code) + "]";

                                crashHandler(tag, errorInfo);

                                removeAt(i);
                                continue;
                            }
                        } else {
                            function(args...);
                        }
                        #else
                        function(args...);
                        #endif

                        ++i;
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
            for (size_t i = 0; i < functions.size(); ) {
                auto& function = functions[i];
                #ifdef DORIAX_CRASH_GUARD
                // Use crash protection if handler is registered
                auto& tag = tags[i];
                auto& crashHandler = FunctionSubscribeGlobal::getCrashHandler();
                if (crashHandler) {
                    Ret result{};
                    CrashInfo ci{};
                    bool ok = callWithCrashGuard([&](){
                        result = function(args...);
                    }, &ci);

                    if (ok) {
                        return result;
                    } else {
                        std::string errorInfo = std::string(ci.name ? ci.name : "UNKNOWN") + 
                                            " (" + (ci.description ? ci.description : "") + 
                                            ") [code=" + std::to_string(ci.code) + "]";

                        crashHandler(tag, errorInfo);

                        removeAt(i);
                        continue;
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

        void clear(){
            functions.clear();
            tags.clear();
        }
    };
}

#endif //FUNCTIONSUBSCRIBE_H