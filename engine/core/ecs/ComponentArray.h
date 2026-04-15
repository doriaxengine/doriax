//
// (c) 2026 Eduardo Doria.
//

#ifndef COMPONENTARRAY_H
#define COMPONENTARRAY_H
    
#include <array>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <numeric>
#include <memory>
#include "Entity.h"
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
        std::unordered_map<Entity, size_t> entityToIndexMap{};
        std::unordered_map<size_t, Entity> indexToEntityMap{};

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
        void insert(Entity entity, T component) {
            if (entityToIndexMap.find(entity) == entityToIndexMap.end()){

                size_t newIndex = componentArray.size();

                entityToIndexMap[entity] = newIndex;
                indexToEntityMap[newIndex] = entity;

                componentArray.push_back(component);

            } else {
                Log::error("Component added to same entity more than once");
            }
        }

        void remove(Entity entity) {

            if (entityToIndexMap.find(entity) != entityToIndexMap.end()){

                // Remove component
                size_t indexOfRemovedEntity = entityToIndexMap[entity];
                componentArray.erase(componentArray. begin() + indexOfRemovedEntity);
                size_t componentSize = componentArray.size();
                
                // Update maps
                for (size_t i = indexOfRemovedEntity; i < componentSize; i++){
                    Entity temp = indexToEntityMap[i+1];
                    entityToIndexMap[temp] = i;
                    indexToEntityMap[i] = temp;
                }
                
                entityToIndexMap.erase(entity);
                indexToEntityMap.erase(componentSize);

            } else {
                Log::error("Removing non-existent component");
            }
        }

        void moveEntityRangeToIndex(Entity start, Entity end, size_t newIndex){

            size_t startIndex = entityToIndexMap[start];
            size_t endIndex = entityToIndexMap[end];
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
                    entityToIndexMap[moved] = i;
                    indexToEntityMap[i] = moved;
                }
            }else{
                for (size_t i = oldIndex; i < newIndex; i++){
                    Entity moved = indexToEntityMap[i+length];
                    entityToIndexMap[moved] = i;
                    indexToEntityMap[i] = moved;
                }
            }

            for (int i = 0; i < tmp.size(); i++){
                entityToIndexMap[tmp[i]] = newIndex+i;
                indexToEntityMap[newIndex+i] = tmp[i];
            }

            moveRange(oldIndex, length, newIndex);
        }

        void moveEntityToIndex(Entity entity, size_t newIndex){

            size_t oldIndex = entityToIndexMap[entity];

            if (oldIndex > newIndex){
                for (size_t i = oldIndex; i > newIndex; i--){
                    Entity moved = indexToEntityMap[i-1];
                    entityToIndexMap[moved] = i;
                    indexToEntityMap[i] = moved;
                }
            }else{
                for (size_t i = oldIndex; i < newIndex; i++){
                    Entity moved = indexToEntityMap[i+1];
                    entityToIndexMap[moved] = i;
                    indexToEntityMap[i] = moved;
                }
            }

            entityToIndexMap[entity] = newIndex;
            indexToEntityMap[newIndex] = entity;

            moveRange(oldIndex, 1, newIndex);
        }

        template<typename C>
        void sortByComponent(std::shared_ptr<ComponentArray<C>> otherComponent) {
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

            // Create new arrays/maps
            std::vector<T> newComponentArray(componentArray.size());
            std::unordered_map<Entity, size_t> newEntityToIndexMap;
            std::unordered_map<size_t, Entity> newIndexToEntityMap;

            for (size_t newIndex = 0; newIndex < indices.size(); ++newIndex) {
                size_t oldIndex = indices[newIndex];
                Entity entity = indexToEntityMap[oldIndex];
                newComponentArray[newIndex] = componentArray[oldIndex];
                newEntityToIndexMap[entity] = newIndex;
                newIndexToEntityMap[newIndex] = entity;
            }

            // Assign new arrays/maps back
            componentArray = std::move(newComponentArray);
            entityToIndexMap = std::move(newEntityToIndexMap);
            indexToEntityMap = std::move(newIndexToEntityMap);
        }

        bool hasEntity(Entity entity) {
            auto it = entityToIndexMap.find(entity);

            if (it == entityToIndexMap.end()) {
                 return false;
            }

            return true;
        }

        T* findComponent(Entity entity) {
            auto it = entityToIndexMap.find(entity);

            if (it == entityToIndexMap.end()) {
                 return NULL;
            }

            return &componentArray[it->second];
        }

        T& getComponent(Entity entity) {
            try{

                return componentArray[entityToIndexMap.at(entity)];

            }catch (const std::out_of_range& e){
                Log::error("Retrieving non-existent component: %s", e.what());
                throw;
            }
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
            try{

                return entityToIndexMap.at(entity);

            }catch (const std::out_of_range& e){
                Log::error("Retrieving non-existent component: %s", e.what());
                throw;
            }
        }

        Entity getEntity(size_t index){
            if (indexToEntityMap.find(index) == indexToEntityMap.end()){
                Log::error("Entity not found");
                return NULL_ENTITY;
            }

            return indexToEntityMap[index];
        }

        size_t size(){
            return componentArray.size();
        }

        void entityDestroyed(Entity entity) override {
            if (entityToIndexMap.find(entity) != entityToIndexMap.end()) {
                remove(entity);
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
