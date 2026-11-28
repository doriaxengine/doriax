// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef COMPONENTARRAY_H
#define COMPONENTARRAY_H
    
#include <array>
#include <cassert>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <numeric>
#include <memory>
#include <utility>
#include "Entity.h"
#include "SparseArray.h"
#include "Log.h"

namespace doriax {

    class DORIAX_API ComponentArrayBase{
    public:
        virtual ~ComponentArrayBase() = default;
        virtual void entityDestroyed(Entity entity) = 0;
    };


    template<typename T>
    class ComponentArray : public ComponentArrayBase{
    private:
        std::vector<T> componentArray{};
        SparseArray entityToIndexMap{};
        std::vector<Entity> indexToEntityMap{};

        template<typename... Args>
        void emplace(Entity entity, Args&&... args) {
            if (entityToIndexMap.contains(entity)) {
                Log::error("Component added to same entity more than once");
                return;
            }

            size_t newIndex = componentArray.size();
            componentArray.emplace_back(std::forward<Args>(args)...);
            entityToIndexMap.set(entity, newIndex);
            indexToEntityMap.push_back(entity);
        }

        void removeAt(size_t index) {
            Entity entity = indexToEntityMap[index];

            // Remove component
            componentArray.erase(componentArray.begin() + index);
            size_t componentSize = componentArray.size();

            // Update maps
            for (size_t i = index; i < componentSize; i++){
                Entity temp = indexToEntityMap[i+1];
                entityToIndexMap.set(temp, i);
                indexToEntityMap[i] = temp;
            }

            entityToIndexMap.erase(entity);
            indexToEntityMap.pop_back();
        }

        void moveRange(size_t start, size_t length, size_t dst){
            typename std::vector<T>::iterator first, middle, last;
            if (start < dst){
                first  = componentArray.begin() + start;
                middle = first + length;
                last   = componentArray.begin() + dst + length;
            }else{
                first  = componentArray.begin() + dst;
                middle = componentArray.begin() + start;
                last   = middle + length;
            }
            std::rotate(first, middle, last);
        }

    public:
        void insert(Entity entity) {
            emplace(entity);
        }

        void insert(Entity entity, const T& component) {
            emplace(entity, component);
        }

        void insert(Entity entity, T&& component) {
            emplace(entity, std::move(component));
        }

        void remove(Entity entity) {
            size_t index = entityToIndexMap.find(entity);
            if (index != SparseArray::NONE){
                removeAt(index);
            } else {
                Log::error("Removing non-existent component");
            }
        }

        void moveEntityRangeToIndex(Entity start, Entity end, size_t newIndex){

            size_t startIndex = entityToIndexMap.find(start);
            size_t endIndex = entityToIndexMap.find(end);
            if (startIndex == SparseArray::NONE || endIndex == SparseArray::NONE || startIndex > endIndex){
                Log::error("Cannot move entity range without components");
                return;
            }
            size_t length = endIndex - startIndex + 1;
            size_t oldIndex = startIndex;

            if (newIndex < 0 || (newIndex + length) > componentArray.size()){
                Log::error("Cannot move entity range out of array");
                return;
            }

            std::vector<Entity> tmp(length);
            for (int i = 0; i < length; i++){
                tmp[i] = indexToEntityMap[oldIndex+i];
            }

            if (oldIndex > newIndex){
                for (size_t i = oldIndex+length-1; i > newIndex+length-1; i--){
                    Entity moved = indexToEntityMap[i-length];
                    entityToIndexMap.set(moved, i);
                    indexToEntityMap[i] = moved;
                }
            }else{
                for (size_t i = oldIndex; i < newIndex; i++){
                    Entity moved = indexToEntityMap[i+length];
                    entityToIndexMap.set(moved, i);
                    indexToEntityMap[i] = moved;
                }
            }

            for (int i = 0; i < tmp.size(); i++){
                entityToIndexMap.set(tmp[i], newIndex+i);
                indexToEntityMap[newIndex+i] = tmp[i];
            }

            moveRange(oldIndex, length, newIndex);
        }

        void moveEntityToIndex(Entity entity, size_t newIndex){

            size_t oldIndex = entityToIndexMap.find(entity);
            if (oldIndex == SparseArray::NONE || newIndex >= componentArray.size()){
                Log::error("Cannot move entity to index");
                return;
            }

            if (oldIndex > newIndex){
                for (size_t i = oldIndex; i > newIndex; i--){
                    Entity moved = indexToEntityMap[i-1];
                    entityToIndexMap.set(moved, i);
                    indexToEntityMap[i] = moved;
                }
            }else{
                for (size_t i = oldIndex; i < newIndex; i++){
                    Entity moved = indexToEntityMap[i+1];
                    entityToIndexMap.set(moved, i);
                    indexToEntityMap[i] = moved;
                }
            }

            entityToIndexMap.set(entity, newIndex);
            indexToEntityMap[newIndex] = entity;

            moveRange(oldIndex, 1, newIndex);
        }

        template<typename C>
        void sortByComponent(ComponentArray<C>* otherComponent) {
            // Create a vector of indices [0, 1, ..., N-1]
            std::vector<size_t> indices(componentArray.size());
            std::iota(indices.begin(), indices.end(), 0);

            // Sort indices based on the key in otherComponent
            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                Entity ea = indexToEntityMap[a];
                Entity eb = indexToEntityMap[b];

                bool hasA = otherComponent->hasEntity(ea);
                bool hasB = otherComponent->hasEntity(eb);

                if (!hasA && !hasB) return false;
                if (!hasA) return false;
                if (!hasB) return true;

                size_t indexA = otherComponent->getIndex(ea);
                size_t indexB = otherComponent->getIndex(eb);
                return indexA < indexB;
            });

            // Create new arrays, moved when that cannot throw
            std::vector<T> newComponentArray;
            newComponentArray.reserve(componentArray.size());
            std::vector<Entity> newIndexToEntityMap(componentArray.size());

            for (size_t newIndex = 0; newIndex < indices.size(); ++newIndex) {
                size_t oldIndex = indices[newIndex];
                newComponentArray.push_back(std::move_if_noexcept(componentArray[oldIndex]));
                newIndexToEntityMap[newIndex] = indexToEntityMap[oldIndex];
            }

            // Assign new arrays back, then their indices
            componentArray = std::move(newComponentArray);
            indexToEntityMap = std::move(newIndexToEntityMap);
            for (size_t i = 0; i < indexToEntityMap.size(); i++) {
                entityToIndexMap.set(indexToEntityMap[i], i);
            }
        }

        bool hasEntity(Entity entity) {
            return entityToIndexMap.contains(entity);
        }

        // NONE when the entity has no component
        size_t findIndex(Entity entity) const {
            return entityToIndexMap.find(entity);
        }

        T* findComponent(Entity entity) {
            size_t index = entityToIndexMap.find(entity);

            if (index == SparseArray::NONE) {
                 return NULL;
            }

            return &componentArray[index];
        }

        T& getComponent(Entity entity) {
            size_t index = entityToIndexMap.find(entity);

            if (index == SparseArray::NONE) {
                Log::error("Retrieving non-existent component");
                throw std::out_of_range("Retrieving non-existent component");
            }

            return componentArray[index];
        }

        T* findComponentFromIndex(size_t index) {
            if (index >= componentArray.size()){
                return NULL;
            }

            return &componentArray[index];
        }

        T& getComponentFromIndex(size_t index) {
            try{

                return componentArray.at(index);

            }catch (const std::out_of_range& e){
                Log::error("Retrieving non-existent component: %s", e.what());
                throw;
            }
        }

        size_t getIndex(Entity entity){
            size_t index = entityToIndexMap.find(entity);

            if (index == SparseArray::NONE) {
                Log::error("Retrieving non-existent component");
                throw std::out_of_range("Retrieving non-existent component");
            }

            return index;
        }

        Entity getEntity(size_t index){
            if (index >= indexToEntityMap.size()){
                Log::error("Entity not found");
                return NULL_ENTITY;
            }

            return indexToEntityMap[index];
        }

        size_t size(){
            return componentArray.size();
        }

        void entityDestroyed(Entity entity) override {
            size_t index = entityToIndexMap.find(entity);
            if (index != SparseArray::NONE) {
                removeAt(index);
            }
        }

        void iterateAll(std::function<void(T component)> lambda){
              for(int i=0;i<componentArray.size();i++){
                lambda(componentArray[i]);
              }
        }
    };

}

#endif //COMPONENTARRAY_H
