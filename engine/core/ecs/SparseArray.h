// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SPARSEARRAY_H
#define SPARSEARRAY_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Entity.h"

namespace doriax {

    // Entity to dense index, in pages that are freed when empty
    class SparseArray {
    private:
        static constexpr size_t PAGE_BITS = 10;
        static constexpr size_t PAGE_SIZE = size_t(1) << PAGE_BITS;
        // ids from 2^24 on are kept in a map, not in pages
        static constexpr size_t MAX_PAGES = size_t(1) << (24 - PAGE_BITS);
        static constexpr uint32_t EMPTY = std::numeric_limits<uint32_t>::max();

        std::vector<std::unique_ptr<uint32_t[]>> pages;
        std::vector<uint32_t> pageCount;
        std::unordered_map<Entity, uint32_t> farIndices;

    public:
        static constexpr size_t NONE = std::numeric_limits<size_t>::max();

        // owns its pages, an exported class must not get a copy
        SparseArray() = default;
        SparseArray(const SparseArray&) = delete;
        SparseArray& operator=(const SparseArray&) = delete;
        SparseArray(SparseArray&&) = default;
        SparseArray& operator=(SparseArray&&) = default;

        size_t find(Entity entity) const {
            size_t page = entity >> PAGE_BITS;
            if (page < pages.size()){
                if (const uint32_t* slots = pages[page].get()){
                    uint32_t index = slots[entity & (PAGE_SIZE - 1)];
                    if (index != EMPTY){
                        return index;
                    }
                }
            }else if (page >= MAX_PAGES){
                auto it = farIndices.find(entity);
                if (it != farIndices.end()){
                    return it->second;
                }
            }

            return NONE;
        }

        bool contains(Entity entity) const {
            return find(entity) != NONE;
        }

        void set(Entity entity, size_t index) {
            assert(index < EMPTY);

            size_t page = entity >> PAGE_BITS;
            if (page >= MAX_PAGES){
                farIndices[entity] = (uint32_t)index;
                return;
            }
            if (page >= pages.size()){
                pages.resize(page + 1);
                pageCount.resize(page + 1, 0);
            }
            if (!pages[page]){
                pages[page].reset(new uint32_t[PAGE_SIZE]);
                std::fill_n(pages[page].get(), PAGE_SIZE, EMPTY);
            }

            uint32_t& slot = pages[page][entity & (PAGE_SIZE - 1)];
            if (slot == EMPTY){
                pageCount[page]++;
            }
            slot = (uint32_t)index;
        }

        void erase(Entity entity) {
            size_t page = entity >> PAGE_BITS;
            if (page >= MAX_PAGES){
                farIndices.erase(entity);
                return;
            }
            if (page >= pages.size() || !pages[page]){
                return;
            }

            uint32_t& slot = pages[page][entity & (PAGE_SIZE - 1)];
            if (slot != EMPTY){
                slot = EMPTY;
                if (--pageCount[page] == 0){
                    pages[page].reset();
                    while (!pages.empty() && !pages.back()){
                        pages.pop_back();
                        pageCount.pop_back();
                    }
                }
            }
        }
    };

}

#endif //SPARSEARRAY_H
