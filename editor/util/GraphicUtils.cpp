#include "GraphicUtils.h"

#include "Out.h"
#include "stb_image_write.h"
#include "util/STBText.h"

#include <future>
#include <thread>

#ifndef USE_GL_READPIXELS
#define USE_GL_READPIXELS 0
#endif

#if defined(SOKOL_METAL)
    #include <TargetConditionals.h>
#endif

#if defined(SOKOL_D3D11)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <d3d11.h>
    #include <wrl/client.h>
    using Microsoft::WRL::ComPtr;
#endif

#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    #ifdef __APPLE__
        #include <OpenGL/gl.h>
    #elif defined(_WIN32)
        #include <windows.h>
        #include <GL/gl.h>
    #else
        #define GL_GLEXT_PROTOTYPES
        #include <GL/gl.h>
    #endif

    #if USE_GL_READPIXELS && defined(_WIN32)
        typedef void (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
        static PFNGLBINDFRAMEBUFFERPROC glBindFramebufferPtr = nullptr;
        #define glBindFramebuffer glBindFramebufferPtr

        #ifndef GL_FRAMEBUFFER
        #define GL_FRAMEBUFFER 0x8D40
        #endif
    #endif
#endif

using namespace doriax;

Vector2 editor::GraphicUtils::getUILayoutCenter(Scene* scene, Entity entity, const UILayoutComponent& layout) {
    Vector2 center = Vector2(0, 0);
    Signature signature = scene->getSignature(entity);
    if (signature.test(scene->getComponentId<TextComponent>())) {
        TextComponent& text = scene->getComponent<TextComponent>(entity);
        if (text.pivotBaseline) {
            center.y = text.stbtext->getAscent();
        }
        if (text.pivotCentered) {
            center.x = layout.width / 2.0f;
        }
    }
    return center;
}

void editor::GraphicUtils::saveFramebufferImage(Framebuffer* framebuffer, fs::path path, bool flipY, std::function<void()> onComplete) {
    uint8_t* pixels = nullptr;
    bool needDelete = false;

    unsigned int width = framebuffer->getWidth();
    unsigned int height = framebuffer->getHeight();

    #if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        // OpenGL
        pixels = new uint8_t[width * height * 4];
        needDelete = true;

        #if USE_GL_READPIXELS
            #if defined(_WIN32)
                if (!glBindFramebufferPtr) {
                    glBindFramebufferPtr = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
                    if (!glBindFramebufferPtr) {
                        delete[] pixels;
                        Out::error("Engine failure: Failed to load glBindFramebuffer");
                        return;
                    }
                }
            #endif
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->getRender().getGLHandler());
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        #else
            glBindTexture(GL_TEXTURE_2D, framebuffer->getRender().getColorTexture().getGLHandler());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glBindTexture(GL_TEXTURE_2D, 0);
        #endif
    #endif

    #if defined(SOKOL_METAL)
        // Metal
        auto metalTexPtr = framebuffer->getRender().getColorTexture().getMetalHandler();
        id<MTLTexture> tex = (__bridge id<MTLTexture>)metalTexPtr;
        id<MTLDevice> device = [tex device];
        NSUInteger bytesPerRow = width * 4;
        NSUInteger dataSize = bytesPerRow * height;
        id<MTLBuffer> buffer = [device newBufferWithLength:dataSize options:MTLResourceStorageModeShared];

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];

        [blit copyFromTexture:tex
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toBuffer:buffer
           destinationOffset:0
      destinationBytesPerRow:bytesPerRow
    destinationBytesPerImage:dataSize];
        [blit endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];

        pixels = (uint8_t*)[buffer contents];
        needDelete = false; // Don't delete!
    #endif

    #if defined(SOKOL_D3D11)
        // D3D11
        auto rtvPtr = framebuffer->getRender().getD3D11HandlerColorRTV();
        ID3D11RenderTargetView* rtv = reinterpret_cast<ID3D11RenderTargetView*>(const_cast<void*>(rtvPtr));

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ID3D11Resource* renderTargetResource = nullptr;
        D3D11_TEXTURE2D_DESC desc = {};
        ID3D11Texture2D* staging = nullptr;
        pixels = new uint8_t[width * height * 4];
        needDelete = true;

        rtv->GetDevice(&device);
        device->GetImmediateContext(&context);
        rtv->GetResource(&renderTargetResource);
        ID3D11Texture2D* srcTex = nullptr;
        renderTargetResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&srcTex);
        srcTex->GetDesc(&desc);

        // Create staging texture
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        device->CreateTexture2D(&stagingDesc, nullptr, &staging);

        // Copy to staging
        context->CopyResource(staging, srcTex);

        // Map and read
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);

        for (int y = 0; y < height; ++y) {
            memcpy(&pixels[y * width * 4],
                (uint8_t*)mapped.pData + y * mapped.RowPitch,
                width * 4);
        }

        // Cleanup
        context->Unmap(staging, 0);
        staging->Release();
        srcTex->Release();
        renderTargetResource->Release();
        context->Release();
        device->Release();
    #endif

    std::thread([pixels, needDelete, width, height, path, flipY, onComplete]() {
        stbi_flip_vertically_on_write(flipY ? 1 : 0);
        stbi_write_png(path.string().c_str(), width, height, 4, pixels, width * 4);
        stbi_flip_vertically_on_write(0);

        if (needDelete && pixels) {
            delete[] pixels;
        }

        // Call the callback when the save operation is complete
        if (onComplete) {
            onComplete();
        }
    }).detach();
}