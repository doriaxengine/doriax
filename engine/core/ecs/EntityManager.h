// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef ENTITYMANAGER_H
#define ENTITYMANAGER_H

#include <set>
#include <vector>
#include <string>
#include <limits>
#include "Entity.h"
#include "Signature.h"
#include "SparseArray.h"
#include "Log.h"

namespace doriax{

    class DORIAX_API EntityManager {
    private:
        static constexpr Entity FIRST_SYSTEM_ENTITY = 1;
        static constexpr Entity LAST_SYSTEM_ENTITY  = 1000;
        static constexpr Entity FIRST_USER_ENTITY   = LAST_SYSTEM_ENTITY + 1;

        Entity lastUserEntity = LAST_SYSTEM_ENTITY; // next user entity starts at 1001

        // one row per entity, signatures apart from the names
        SparseArray rows;
        std::vector<Entity> rowEntities;
        std::vector<Signature> signatures;
        std::vector<std::string> names;
        std::set<Entity> entities; // the same ids in order, as getEntityList returns them

        size_t getRow(Entity entity){
            size_t row = rows.find(entity);
            if (row == SparseArray::NONE){
                row = rowEntities.size();
                rows.set(entity, row);
                rowEntities.push_back(entity);
                signatures.emplace_back();
                names.emplace_back();
                entities.insert(entity);
            }
            return row;
        }

    public:

        static constexpr Entity firstSystemEntity() { return FIRST_SYSTEM_ENTITY; }
        static constexpr Entity lastSystemEntity()  { return LAST_SYSTEM_ENTITY; }
        static constexpr Entity firstUserEntity()   { return FIRST_USER_ENTITY; }

        bool recreateEntity(Entity entity) { // for internal editor use only
            if (!isCreated(entity)){
                getRow(entity);
                return true;
            }
            return false;
        }

        void setLastUserEntity(Entity entity){
            if (entity < LAST_SYSTEM_ENTITY) {
                this->lastUserEntity = LAST_SYSTEM_ENTITY;
            } else {
                this->lastUserEntity = entity;
            }
        }

        Entity getLastUserEntity() const{
            return this->lastUserEntity;
        }

        // System entity in range [1,100], first free slot
        Entity createSystemEntity() {
            for (Entity e = FIRST_SYSTEM_ENTITY; e <= LAST_SYSTEM_ENTITY; ++e){
                if (!rows.contains(e)){
                    getRow(e);
                    return e;
                }
            }
            Log::error("No free system entity slot available in range [1..100]");
            return NULL_ENTITY;
        }

        // User entity in range [101..max], monotonically increasing, skipping used ids
        Entity createUserEntity() {
            if (lastUserEntity < LAST_SYSTEM_ENTITY){
                lastUserEntity = LAST_SYSTEM_ENTITY;
            }

            Entity candidate = lastUserEntity;
            while (true){
                if (candidate == std::numeric_limits<Entity>::max()){
                    Log::error("User entity counter overflow");
                    return NULL_ENTITY;
                }
                candidate++;

                // Safety: if wrapped into system range by some means, jump to first user id
                if (candidate <= LAST_SYSTEM_ENTITY){
                    candidate = FIRST_USER_ENTITY;
                }

                if (!rows.contains(candidate)){
                    break;
                }
            }

            lastUserEntity = candidate;
            getRow(candidate);
            return candidate;
        }

        bool isCreated(Entity entity) const {
            return rows.contains(entity);
        }

        void destroy(Entity entity) {
            size_t row = rows.find(entity);
            if (row != SparseArray::NONE){
                // the last row fills the gap
                size_t last = rowEntities.size() - 1;
                if (row != last){
                    rowEntities[row] = rowEntities[last];
                    signatures[row] = signatures[last];
                    names[row] = std::move(names[last]);
                    rows.set(rowEntities[row], row);
                }
                rowEntities.pop_back();
                signatures.pop_back();
                names.pop_back();
                rows.erase(entity);
            }
            entities.erase(entity);
        }

        std::vector<Entity> getEntityList() const{
            return std::vector<Entity>(entities.begin(), entities.end());
        }

        void setSignature(Entity entity, Signature signature) {
            if (!rows.contains(entity)){
                Log::error("Entity does not exist to set signature");
            }
            signatures[getRow(entity)] = signature;
        }

        Signature getSignature(Entity entity) const{
            size_t row = rows.find(entity);
            if (row == SparseArray::NONE){
                Log::error("Entity does not exist to get signature");
                return Signature();
            }
            return signatures[row];
        }

        void setName(Entity entity, const std::string& name) {
            if (!rows.contains(entity)){
                Log::error("Entity does not exist to set name");
            }
            names[getRow(entity)] = name;
        }

        std::string getName(Entity entity) const{
            size_t row = rows.find(entity);
            if (row == SparseArray::NONE){
                Log::error("Entity does not exist to get name");
                return std::string();
            }
            return names[row];
        }
    };

}

#endif //ENTITYMANAGER_H