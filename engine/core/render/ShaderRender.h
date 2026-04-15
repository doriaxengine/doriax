//
// (c) 2026 Eduardo Doria.
//

#ifndef ShaderRender_h
#define ShaderRender_h

#include "Render.h"
#include "sokol/SokolShader.h"

namespace doriax{
    class DORIAX_API ShaderRender{

    private:
        // render callback clean function
        static void cleanupShader(void* data);

    public:
        //***Backend***
        SokolShader backend;
        //***
        ShaderData shaderData; //For reflection info

        ShaderRender();
        ShaderRender(const ShaderRender& rhs);
        ShaderRender& operator=(const ShaderRender& rhs);
        virtual ~ShaderRender();

        bool createShader(ShaderData& shaderData);
        void destroyShader();
        bool isCreated();
    };
}


#endif /* ShaderRender_h */