// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SOKOLRESOURCECACHE_H
#define SOKOLRESOURCECACHE_H

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace doriax{

    // Immutable resources with equal keys share one handle, counted by its users.
    // Only the render thread creates shared handles, so they are always ready to use.
    template<typename Key, typename Handle>
    class SokolResourceCache{
    private:
        struct Entry{
            Handle handle;
            unsigned int users;
        };

        std::mutex mutex;
        std::map<Key, Entry> byKey;
        std::unordered_map<uint32_t, typename std::map<Key, Entry>::iterator> byId;

    public:
        template<typename Create, typename IsValid>
        Handle get(Key key, Create create, IsValid isValid){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = byKey.find(key);
            if (it != byKey.end()){
                it->second.users++;
                return it->second.handle;
            }

            Handle handle = create();
            if (isValid(handle)){
                it = byKey.emplace(std::move(key), Entry{handle, 1}).first;
                byId[handle.id] = it;
            }
            return handle;
        }

        // false while other users remain; handles not made here are always released
        bool release(Handle handle){
            std::lock_guard<std::mutex> lock(mutex);
            auto it = byId.find(handle.id);
            if (it == byId.end())
                return true;
            if (--it->second->second.users > 0)
                return false;
            byKey.erase(it->second);
            byId.erase(it);
            return true;
        }

        // the handles belong to the sokol context being shut down
        void clear(){
            std::lock_guard<std::mutex> lock(mutex);
            byKey.clear();
            byId.clear();
        }
    };

}

#endif //SOKOLRESOURCECACHE_H
