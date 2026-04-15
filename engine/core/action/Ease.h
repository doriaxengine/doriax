//
// (c) 2026 Eduardo Doria.
//

#ifndef EASE_H
#define EASE_H

#define _USE_MATH_DEFINES
#include <math.h>

namespace doriax {

    enum class EaseType{
        LINEAR,
        QUAD_IN,
        QUAD_OUT,
        QUAD_IN_OUT,
        CUBIC_IN,
        CUBIC_OUT,
        CUBIC_IN_OUT,
        QUART_IN,
        QUART_OUT,
        QUART_IN_OUT,
        QUINT_IN,
        QUINT_OUT,
        QUINT_IN_OUT,
        SINE_IN,
        SINE_OUT,
        SINE_IN_OUT,
        EXPO_IN,
        EXPO_OUT,
        EXPO_IN_OUT,
        CIRC_IN,
        CIRC_OUT,
        CIRC_IN_OUT,
        ELASTIC_IN,
        ELASTIC_OUT,
        ELASTIC_IN_OUT,
        BACK_IN,
        BACK_OUT,
        BACK_IN_OUT,
        BOUNCE_IN,
        BOUNCE_OUT,
        BOUNCE_IN_OUT
    };

    class Ease {

        public:

        static inline float linear(float time){
            return time;
        }

        static inline float easeInQuad(float time){
            return time * time;
        }

        static inline float easeOutQuad(float time){
            return time * (2 - time);
        }

        static inline float easeInOutQuad(float time){
            if ((time *= 2) < 1) {
                return 0.5f * time * time;
            }

            time--;
            return - 0.5f * (time * (time - 2) - 1);
        }

        static inline float easeInCubic(float time){
            return time * time * time;
        }

        static inline float easeOutCubic(float time){
            time--;
            return time * time * time + 1;
        }

        static inline float easeInOutCubic(float time){
            if ((time *= 2) < 1) {
                return 0.5f * time * time * time;
            }
            time -= 2;
            return 0.5f * (time * time * time + 2);
        }

        static inline float easeInQuart(float time){
            return time * time * time * time;
        }

        static inline float easeOutQuart(float time){
            time--;
            return 1 - (time * time * time * time);
        }

        static inline float easeInOutQuart(float time){
            if ((time *= 2) < 1) {
                return 0.5f * time * time * time * time;
            }
            time -= 2;
            return - 0.5f * (time * time * time * time - 2);
        }

        static inline float easeInQuint(float time){
            return time * time * time * time * time;
        }

        static inline float easeOutQuint(float time){
            time--;
            return time * time * time * time * time + 1;
        }

        static inline float easeInOutQuint(float time){
            if ((time *= 2) < 1) {
                return 0.5f * time * time * time * time * time;
            }

            time -= 2;
            return 0.5f * (time * time * time * time * time + 2);
        }

        static inline float easeInSine(float time){
            return 1 - (float)cos(time * M_PI / 2);
        }

        static inline float easeOutSine(float time){
            return (float)sin(time * M_PI / 2);
        }

        static inline float easeInOutSine(float time){
            return 0.5f * (1 - (float)cos(M_PI * time));
        }

        static inline float easeInExpo(float time){
            return time == 0 ? 0 : (float)pow(1024, time - 1);
        }

        static inline float easeOutExpo(float time){
            return time == 1 ? 1 : 1 - (float)pow(2, - 10 * time);
        }

        static inline float easeInOutExpo(float time){
            if (time == 0) {
                return 0;
            }

            if (time == 1) {
                return 1;
            }

            if ((time *= 2) < 1) {
                return 0.5f * (float)pow(1024, time - 1);
            }

            return 0.5f * (- (float)pow(2, - 10 * (time - 1)) + 2);
        }

        static inline float easeInCirc(float time){
            return 1 - sqrt(1 - time * time);
        }

        static inline float easeOutCirc(float time){
            time--;
            return sqrt(1 - (time * time));
        }

        static inline float easeInOutCirc(float time){
            if ((time *= 2) < 1) {
                return - 0.5f * (sqrt(1 - time * time) - 1);
            }

            time -= 2;
            return 0.5f * (sqrt(1 - time * time) + 1);
        }

        static inline float easeInElastic(float time){
            if (time == 0) {
                return 0;
            }

            if (time == 1) {
                return 1;
            }

            return -(float)pow(2, 10 * (time - 1)) * (float)sin((time - 1.1) * 5 * M_PI);
        }

        static inline float easeOutElastic(float time){
            if (time == 0) {
                return 0;
            }

            if (time == 1) {
                return 1;
            }

            return (float)pow(2, -10 * time) * (float)sin((time - 0.1f) * 5 * M_PI) + 1;
        }

        static inline float easeInOutElastic(float time){
            if (time == 0) {
                return 0;
            }

            if (time == 1) {
                return 1;
            }

            time *= 2;

            if (time < 1) {
                return -0.5f * (float)pow(2, 10 * (time - 1)) * (float)sin((time - 1.1f) * 5 * M_PI);
            }

            return 0.5f * (float)pow(2, -10 * (time - 1)) * (float)sin((time - 1.1f) * 5 * M_PI) + 1;
        }

        static inline float easeInBack(float time){
            float s = 1.70158f;

            return time * time * ((s + 1) * time - s);
        }

        static inline float easeOutBack(float time){
            float s = 1.70158f;

            time--;
            return time * time * ((s + 1) * time + s) + 1;
        }

        static inline float easeInOutBack(float time){
            float s = 1.70158f * 1.525f;

            if ((time *= 2) < 1) {
                return 0.5f * (time * time * ((s + 1) * time - s));
            }

            time -= 2;
            return 0.5f * (time * time * ((s + 1) * time + s) + 2);
        }

        static inline float easeOutBounce(float time){
            if (time < (1 / 2.75f)) {
                return 7.5625f * time * time;
            } else if (time < (2 / 2.75f)) {
                time -= (1.5f / 2.75f);
                return 7.5625f * time * time + 0.75f;
            } else if (time < (2.5f / 2.75f)) {
                time -= (2.25f / 2.75f);
                return 7.5625f * time * time + 0.9375f;
            } else {
                time -= (2.625f / 2.75f);
                return 7.5625f * time * time + 0.984375f;
            }
        }

        static inline float easeInBounce(float time){
            return 1 - easeOutBounce(1 - time);
        }

        static inline float easeInOutBounce(float time){
            if (time < 0.5f) {
                return easeInBounce(time * 2) * 0.5f;
            }

            return easeOutBounce(time * 2 - 1) * 0.5f + 0.5f;
        }

        static inline std::function<float(float)> getFunction(EaseType functionType){
            if (functionType == EaseType::LINEAR){
                return Ease::linear;
            }else if(functionType == EaseType::QUAD_IN){
                return Ease::easeInQuad;
            }else if(functionType == EaseType::QUAD_OUT){
                return Ease::easeOutQuad;
            }else if(functionType == EaseType::QUAD_IN_OUT){
                return Ease::easeInOutQuad;
            }else if(functionType == EaseType::CUBIC_IN){
                return Ease::easeInCubic;
            }else if(functionType == EaseType::CUBIC_OUT){
                return Ease::easeOutCubic;
            }else if(functionType == EaseType::CUBIC_IN_OUT){
                return Ease::easeInOutCubic;
            }else if(functionType == EaseType::QUART_IN){
                return Ease::easeInQuart;
            }else if(functionType == EaseType::QUART_OUT){
                return Ease::easeOutQuart;
            }else if(functionType == EaseType::QUART_IN_OUT){
                return Ease::easeInOutQuart;
            }else if(functionType == EaseType::QUINT_IN){
                return Ease::easeInQuint;
            }else if(functionType == EaseType::QUINT_OUT){
                return Ease::easeOutQuint;
            }else if(functionType == EaseType::QUINT_IN_OUT){
                return Ease::easeInOutQuint;
            }else if(functionType == EaseType::SINE_IN){
                return Ease::easeInSine;
            }else if(functionType == EaseType::SINE_OUT){
                return Ease::easeOutSine;
            }else if(functionType == EaseType::SINE_IN_OUT){
                return Ease::easeInOutSine;
            }else if(functionType == EaseType::EXPO_IN){
                return Ease::easeInExpo;
            }else if(functionType == EaseType::EXPO_OUT){
                return Ease::easeOutExpo;
            }else if(functionType == EaseType::EXPO_IN_OUT){
                return Ease::easeInOutExpo;
            }else if(functionType == EaseType::CIRC_IN){
                return Ease::easeInCirc;
            }else if(functionType == EaseType::CIRC_OUT){
                return Ease::easeOutCirc;
            }else if(functionType == EaseType::CIRC_IN_OUT){
                return Ease::easeInOutCirc;
            }else if(functionType == EaseType::ELASTIC_IN){
                return Ease::easeInElastic;
            }else if(functionType == EaseType::ELASTIC_OUT){
                return Ease::easeOutElastic;
            }else if(functionType == EaseType::ELASTIC_IN_OUT){
                return Ease::easeInOutElastic;
            }else if(functionType == EaseType::BACK_IN){
                return Ease::easeInBack;
            }else if(functionType == EaseType::BACK_OUT){
                return Ease::easeOutBack;
            }else if(functionType == EaseType::BACK_IN_OUT){
                return Ease::easeInOutBack;
            }else if(functionType == EaseType::BOUNCE_IN){
                return Ease::easeInBounce;
            }else if(functionType == EaseType::BOUNCE_OUT){
                return Ease::easeOutBounce;
            }else if(functionType == EaseType::BOUNCE_IN_OUT){
                return Ease::easeInOutBounce;
            }

            return Ease::linear;
        }
    };
}


#endif //EASE_H
