//
//  ViewController.m
//  Doriax macOS
//
//  Created by Eduardo Dória Lima on 24/12/20.
//

#import "ViewController.h"
#import "Renderer.h"
#import "EngineView.h"

@implementation ViewController
{
    EngineView *_view;

    Renderer *_renderer;
}

- (void)viewDidDisappear
{
    [_renderer destroyView];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    NSArray *args = [[NSProcessInfo processInfo] arguments];

    _view = (EngineView *)self.view;

    _view.device = MTLCreateSystemDefaultDevice();

    if(!_view.device)
    {
        NSLog(@"Metal is not supported on this device");
        self.view = [[NSView alloc] initWithFrame:self.view.frame];
        return;
    }

    _renderer = [[Renderer alloc] initWithMetalKitView:_view withArgs:args];

    [_renderer mtkView:_view drawableSizeWillChange:_view.drawableSize];

    _view.delegate = _renderer;
}

@end
