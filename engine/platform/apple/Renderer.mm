//
//  Renderer.m
//
//  Created by Eduardo Dória on 24/12/20.
//

#import <simd/simd.h>
#import <ModelIO/ModelIO.h>

#import "Renderer.h"

#include "Engine.h"
#include "DoriaxApple.h"

@implementation Renderer
{
    id <MTLDevice> _device;
}

static MTKView* _view;
static CGSize _screenSize;

+ (MTKView*)view {
    return _view;
}

+ (CGSize)screenSize {
    return _screenSize;
}

-(char**)getArray:(NSArray*)a_array
{
    unsigned long count = [a_array count];
    char **array = (char **)malloc((count + 1) * sizeof(char*));

    for (unsigned i = 0; i < count; i++)
    {
         array[i] = strdup([[a_array objectAtIndex:i] UTF8String]);
    }
    array[count] = NULL;
    return array;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view withArgs:(NSArray*)args;
{
    doriax::Engine::systemInit((int)[args count], [self getArray:args], new DoriaxApple());
    
    self = [super init];
    if(self)
    {
        _view = view;
        _device = view.device;
        
        //[view setPreferredFramesPerSecond:60];
        //[view setSampleCount:4]; //causing issues in real devices when using SG_LOADACTION_LOAD
        [view setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
        [view setDepthStencilPixelFormat:MTLPixelFormatDepth32Float_Stencil8];
        
        _screenSize.width = (float) view.drawableSize.width;
        _screenSize.height = (float) view.drawableSize.height;
        doriax::Engine::systemViewLoaded();
    }

    return self;
}

- (void)destroyView
{
    doriax::Engine::systemViewDestroyed();
    doriax::Engine::systemShutdown();
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    doriax::Engine::systemDraw();
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    /// Respond to drawable size or orientation changes here
    _screenSize.width = (float) size.width;
    _screenSize.height = (float) size.height;
    doriax::Engine::systemViewChanged();
}

+ (void)pauseGame {
    doriax::Engine::systemPause();
}

+ (void)resumeGame {
    doriax::Engine::systemResume();
}

@end
