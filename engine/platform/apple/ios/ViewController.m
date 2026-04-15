//
//  ViewController.m
//  Doriax iOS
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
    _view.backgroundColor = UIColor.blackColor;

    if(!_view.device)
    {
        NSLog(@"Metal is not supported on this device");
        self.view = [[UIView alloc] initWithFrame:self.view.frame];
        return;
    }

    _renderer = [[Renderer alloc] initWithMetalKitView:_view withArgs:args];

    [_renderer mtkView:_view drawableSizeWillChange:_view.drawableSize];

    _view.delegate = _renderer;
    
    // Pause game when application is backgrounded.
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(pauseGame)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];

    // Resume game when application becomes active.
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(resumeGame)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
}

- (void)pauseGame {
    [Renderer pauseGame];
}

- (void)resumeGame {
    [Renderer resumeGame];
}

@end
