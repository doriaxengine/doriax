#import "EngineView.h"

#include "Engine.h"

#define MAX_TOUCHES 10

@implementation EngineView{

}

static UITouch* touches[MAX_TOUCHES];

- (id)initWithCoder:(NSCoder *)coder{
    self = [super initWithCoder:coder];
    
    [self engineInit];
    
    return self;
}

- (id)initWithFrame:(CGRect)frameRect{
    self = [super initWithFrame:frameRect];
    
    [self engineInit];
    
    return self;
}

- (void)engineInit{
    [self setUserInteractionEnabled:YES];
    [self setMultipleTouchEnabled:YES];
}

- (void)insertText:(NSString *)text {
    const NSUInteger length = [text length];
    for (NSUInteger i = 0;  i < length;  i++) {
        const unichar codepoint = [text characterAtIndex:i];
        if ((codepoint & 0xFF00) == 0xF700)
            continue;

        doriax::Engine::systemCharInput(codepoint);
    }
}

- (void)deleteBackward {
    doriax::Engine::systemCharInput('\b');
}

- (BOOL)hasText {
    return YES;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

-(CGPoint)getTouchPoint:(UITouch*)touch{
    const CGPoint pointInView = [touch locationInView:self];
    const CGFloat scale = [UIScreen mainScreen].nativeScale;
    
    const CGFloat pointX = pointInView.x * scale;
    const CGFloat pointY = pointInView.y * scale;
    
    return CGPointMake(pointX, pointY);
}

-(NSUInteger)getTouchId:(UITouch*)touch{
    return [self getTouchId:touch andRemove:false];
}

-(NSInteger)getTouchId:(UITouch*)touch andRemove:(bool)remove{
    NSInteger next = -1;
    for (NSInteger i = 0; i < MAX_TOUCHES; i++) {
        if (touches[i] == touch){
            if (remove)
                touches[i] = NULL;
            return i+1;
        }
        if (next == -1 && touches[i] == NULL) {
            next = i;
        }
    }
    
    if (next != -1) {
        touches[next] = touch;
        return next+1;
    }
    
    return -1;
}

-(void)clearTouches{
    for (int i = 0; i < MAX_TOUCHES; i++) {
        touches[i] = NULL;
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent*)event{
    [super touchesBegan:touches withEvent:event];
    for (UITouch *touch in touches) {
        const CGPoint point = [self getTouchPoint:touch];
        const int identifier = (int)[self getTouchId:touch];
        doriax::Engine::systemTouchStart(identifier, point.x, point.y);
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent*)event{
    [super touchesMoved:touches withEvent:event];
    for (UITouch *touch in touches) {
        const CGPoint point = [self getTouchPoint:touch];
        const int identifier = (int)[self getTouchId:touch];
        doriax::Engine::systemTouchMove(identifier, point.x, point.y);
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent*)event{
    [super touchesEnded:touches withEvent:event];
    for (UITouch *touch in touches) {
        const CGPoint point = [self getTouchPoint:touch];
        const int identifier = (int)[self getTouchId:touch andRemove:true];
        doriax::Engine::systemTouchEnd(identifier, point.x, point.y);
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent*)event{
    [super touchesCancelled:touches withEvent:event];
    [self clearTouches];
    doriax::Engine::systemTouchCancel();
}

@end
