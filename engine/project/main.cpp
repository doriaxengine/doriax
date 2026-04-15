#include "Doriax.h"
using namespace doriax;

#include "Image.h"

Scene scene;
Image image(&scene);

DORIAX_INIT void init(){
    image.setAnchorPreset(AnchorPreset::CENTER);
    image.setTexture("doriax.png");

    Engine::setScalingMode(Scaling::FITWIDTH);
    Engine::setCanvasSize(1000,480);
    Engine::setScene(&scene);
}
