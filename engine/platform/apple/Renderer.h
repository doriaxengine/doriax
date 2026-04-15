//
//  ViewDelegate.h
//
//  Created by Eduardo Dória Lima on 24/12/20.
//

#import <MetalKit/MetalKit.h>

@interface Renderer : NSObject <MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view withArgs:(nullable NSArray*)args;

- (void)destroyView;

+ (void)pauseGame;
+ (void)resumeGame;

@property (class, nonatomic, assign, readonly, nullable) MTKView* view;
@property (class, nonatomic, assign, readonly) CGSize screenSize;

@end

