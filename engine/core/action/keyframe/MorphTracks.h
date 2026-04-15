//
// (c) 2026 Eduardo Doria.
//

#ifndef MORPHTRACKS_H
#define MORPHTRACKS_H

#include "action/Action.h"

namespace doriax{
    class DORIAX_API MorphTracks: public Action{

    public:
        MorphTracks(Scene* scene);
        MorphTracks(Scene* scene, std::vector<float> times, std::vector<std::vector<float>> values);

        void setTimes(std::vector<float> times);
        void setValues(std::vector<std::vector<float>> values);
    };
}

#endif //MORPHTRACKS_H