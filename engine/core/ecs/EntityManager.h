//
// (c) 2026 Eduardo Doria.
//

#ifndef ENTITYMANAGER_H
#define ENTITYMANAGER_H

#include <map>
#include <vector>
#include <string>
#include <limits>
#include "Entity.h"
#include "Signature.h"
#include "Log.h"

namespace doriax{

    struct EntityMetadata{
        std::string name;
        Signature signature;
    };

    class DORIAX_API EntityManager {
    private:
        static constexpr Entity FIRST_SYSTEM_ENTITY = 1;
        static constexpr Entity LAST_SYSTEM_ENTITY  = 100;
        static constexpr Entity FIRST_USER_ENTITY   = LAST_SYSTEM_ENTITY + 1;

        Entity lastUserEntity = LAST_SYSTEM_ENTITY; // next user entity starts at 101
        std::map<Entity, EntityMetadata> metadata;

    public:

        static constexpr Entity firstSystemEntity() { return FIRST_SYSTEM_ENTITY; }
        static constexpr Entity lastSystemEntity()  { return LAST_SYSTEM_ENTITY; }
        static constexpr Entity firstUserEntity()   { return FIRST_USER_ENTITY; }

        bool recreateEntity(Entity entity) { // for internal editor use only
            if (!isCreated(entity)){
                metadata[entity];
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
                if (metadata.count(e) == 0){
                    metadata[e];
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

                if (metadata.count(candidate) == 0){
                    break;
                }
            }

            lastUserEntity = candidate;
            metadata[candidate];
            return candidate;
        }

        bool isCreated(Entity entity) const {
            return metadata.count(entity) > 0;
        }

        void destroy(Entity entity) {
            metadata.erase(entity);
        }

        std::vector<Entity> getEntityList() const{
            std::vector<Entity> list;
            for (auto const& [key, val] : metadata){
                list.push_back(key);
            }
            return list;
        }

        void setSignature(Entity entity, Signature signature) {
            if (metadata.count(entity)==0){
                Log::error("Entity does not exist to set signature");
            }
            metadata[entity].signature = signature;
        }

        Signature getSignature(Entity entity) const{
            if (metadata.count(entity)==0){
                Log::error("Entity does not exist to get signature");
                return Signature();
            }
            return metadata.at(entity).signature;
        }

        void setName(Entity entity, const std::string& name) {
            if (metadata.count(entity)==0){
                Log::error("Entity does not exist to set name");
            }
            metadata[entity].name = name;
        }

        std::string getName(Entity entity) const{
            if (metadata.count(entity)==0){
                Log::error("Entity does not exist to get name");
                return std::string();
            }
            return metadata.at(entity).name;
        }
    };

}

#endif //ENTITYMANAGER_H