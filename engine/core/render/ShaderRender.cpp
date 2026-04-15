//
// (c) 2026 Eduardo Doria.
//

#include "ShaderRender.h"

#include "SystemRender.h"
#include "Engine.h"

using namespace doriax;

ShaderRender::ShaderRender(){ }

ShaderRender::ShaderRender(const ShaderRender& rhs) : backend(rhs.backend), shaderData(rhs.shaderData) { }

ShaderRender& ShaderRender::operator=(const ShaderRender& rhs) { 
    backend = rhs.backend;
    shaderData = rhs.shaderData;
    return *this; 
}

ShaderRender::~ShaderRender(){
    //Cannot destroy because its a handle
}

bool ShaderRender::createShader(ShaderData& shaderData){
    if (Engine::isViewLoaded() && !isCreated()) {
        this->shaderData = shaderData;

        bool ret = backend.createShader(this->shaderData);

        SystemRender::scheduleCleanup(ShaderRender::cleanupShader, &(this->shaderData));
        
        return ret;
    }else{
        return false;
    }
}

void ShaderRender::destroyShader(){
    backend.destroyShader();
}

bool ShaderRender::isCreated(){
    return backend.isCreated();
}

void ShaderRender::cleanupShader(void* data){
    //Keep only reflection info
    static_cast<ShaderData*>(data)->releaseSourceData();
}