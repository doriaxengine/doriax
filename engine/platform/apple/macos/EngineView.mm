#import "EngineView.h"

#include "Engine.h"
#include "Input.h"

@implementation EngineView{
    NSMutableAttributedString* _markedText;
    NSTrackingArea* _trackingArea;
}

static short int keycodes[256];

static const NSRange _emptyRange = { NSNotFound, 0 };

- (void)viewDidMoveToWindow{
    [[self window] setAcceptsMouseMovedEvents:YES];
}

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
    _trackingArea = nil;
    _markedText = [[NSMutableAttributedString alloc] init];
    
    [self createKeyCodeArray];
}

-(void)keyDown:(NSEvent*)event{
    const int key = (int)[self convertKey:[event keyCode]];
    const int mods = (int)[self convertModFlags:[event modifierFlags]];

    doriax::Engine::systemKeyDown(key, [event isARepeat], mods);

    [self interpretKeyEvents:@[event]];
}

-(void)keyUp:(NSEvent*)event{
    const int key = (int)[self convertKey:[event keyCode]];
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    
    doriax::Engine::systemKeyUp(key, [event isARepeat], mods);
}

- (void)flagsChanged:(NSEvent *)event{
    const unsigned int modifierFlags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    const int key = (int)[self convertKey:[event keyCode]];
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const NSUInteger keyFlag = [self convertKeyToModFlag:key];

    if (modifierFlags & keyFlag)
        doriax::Engine::systemKeyDown(key, false, mods);
    else if (modifierFlags | keyFlag)
        doriax::Engine::systemKeyUp(key, false, mods);
 
}

- (void)updateTrackingAreas {
    if(_trackingArea != nil) {
        [self removeTrackingArea:_trackingArea];
        //[trackingArea release];
    }

    int opts = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);
    _trackingArea = [ [NSTrackingArea alloc] initWithRect:[self bounds]
                                                 options:opts
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

-(CGPoint)getMousePoint:(NSEvent*)event{
    const NSPoint pointInView = [event locationInWindow];
    const NSPoint pointInBackingLayer = [self convertPointToBacking:pointInView];
    
    const CGFloat mouseX = pointInBackingLayer.x;
    const CGFloat mouseY = self.drawableSize.height - pointInBackingLayer.y;
    
    return CGPointMake(mouseX, mouseY);
}

- (void)mouseEntered:(NSEvent*)event {
    doriax::Engine::systemMouseEnter();
    if (doriax::Engine::getMouseCursor() != doriax::CursorType::ARROW){
        doriax::Engine::setMouseCursor(doriax::Engine::getMouseCursor());
    }
}

- (void)mouseExited:(NSEvent*)event {
    doriax::Engine::systemMouseLeave();
}

- (void)mouseDown:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseDown(S_MOUSE_BUTTON_LEFT, point.x, point.y, mods);
}

- (void)rightMouseDown:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseDown(S_MOUSE_BUTTON_RIGHT, point.x, point.y, mods);
}

- (void)otherMouseDown:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseDown(S_MOUSE_BUTTON_MIDDLE, point.x, point.y, mods);
}

- (void)mouseUp:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseUp(S_MOUSE_BUTTON_LEFT, point.x, point.y, mods);
}

- (void)rightMouseUp:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseUp(S_MOUSE_BUTTON_RIGHT, point.x, point.y, mods);
}

- (void)otherMouseUp:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseUp(S_MOUSE_BUTTON_MIDDLE, point.x, point.y, mods);
}

- (void)mouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)mouseMoved:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    const CGPoint point = [self getMousePoint:event];
    doriax::Engine::systemMouseMove(point.x, point.y, mods);
}

- (void)scrollWheel:(NSEvent*)event {
    const int mods = (int)[self convertModFlags:[event modifierFlags]];
    float dx = (float) event.scrollingDeltaX;
    float dy = (float) event.scrollingDeltaY;
    if (event.hasPreciseScrollingDeltas) {
        dx *= 0.1;
        dy *= 0.1;
    }
    if (dx != 0.0f || dy != 0.0f) {
        doriax::Engine::systemMouseScroll(dx, dy, mods);
    }
}

- (BOOL)canBecomeKeyView{
    return YES;
}

- (BOOL)acceptsFirstResponder{
    return YES;
}

- (BOOL)wantsUpdateLayer{
    return YES;
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange{
    NSString* characters;

    if ([string isKindOfClass:[NSAttributedString class]])
        characters = [string string];
    else
        characters = (NSString*) string;

    const NSUInteger length = [characters length];
    for (NSUInteger i = 0;  i < length;  i++) {
        const unichar codepoint = [characters characterAtIndex:i];
        if ((codepoint & 0xFF00) == 0xF700)
            continue;

        doriax::Engine::systemCharInput(codepoint);
    }
}

- (void)doCommandBySelector:(SEL)selector{
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange{
    if ([string isKindOfClass:[NSAttributedString class]])
        _markedText = [[NSMutableAttributedString alloc] initWithAttributedString:string];
    else
        _markedText = [[NSMutableAttributedString alloc] initWithString:string];
}

- (void)unmarkText{
    [[_markedText mutableString] setString:@""];
}

- (NSRange)selectedRange{
    return _emptyRange;
}

- (NSRange)markedRange{
    if ([_markedText length] > 0)
        return NSMakeRange(0, [_markedText length] - 1);
    else
        return _emptyRange;
}

- (BOOL)hasMarkedText{
    //return [_markedText length] > 0;
    return true; // to send all keyboard events (ex: backspace) to insertText
}

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
    return nil;
}

- (NSArray*)validAttributesForMarkedText{
    return [NSArray array];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange{
    return NSMakeRect(self.frame.origin.x, self.frame.origin.y, 0.0, 0.0);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point{
    return 0;
}

- (NSInteger)convertModFlags:(NSUInteger)flags{
    int mods = 0;

    if (flags & NSEventModifierFlagShift)
        mods |= S_MODIFIER_SHIFT;
    if (flags & NSEventModifierFlagControl)
        mods |= S_MODIFIER_CONTROL;
    if (flags & NSEventModifierFlagOption)
        mods |= S_MODIFIER_ALT;
    if (flags & NSEventModifierFlagCommand)
        mods |= S_MODIFIER_SUPER;
    if (flags & NSEventModifierFlagCapsLock)
        mods |= S_MODIFIER_CAPS_LOCK;

    return mods;
}

- (NSInteger)convertKey:(NSUInteger)key{
    if (key >= sizeof(keycodes) / sizeof(keycodes[0]))
        return S_KEY_UNKNOWN;

    return keycodes[key];
}

- (NSUInteger)convertKeyToModFlag:(NSInteger)key{
    switch (key) {
        case S_KEY_LEFT_SHIFT:
        case S_KEY_RIGHT_SHIFT:
            return NSEventModifierFlagShift;
        case S_KEY_LEFT_CONTROL:
        case S_KEY_RIGHT_CONTROL:
            return NSEventModifierFlagControl;
        case S_KEY_LEFT_ALT:
        case S_KEY_RIGHT_ALT:
            return NSEventModifierFlagOption;
        case S_KEY_LEFT_SUPER:
        case S_KEY_RIGHT_SUPER:
            return NSEventModifierFlagCommand;
        case S_KEY_CAPS_LOCK:
            return NSEventModifierFlagCapsLock;
    }

    return 0;
}

- (void)createKeyCodeArray{
    keycodes[0x1D] = S_KEY_0;
    keycodes[0x12] = S_KEY_1;
    keycodes[0x13] = S_KEY_2;
    keycodes[0x14] = S_KEY_3;
    keycodes[0x15] = S_KEY_4;
    keycodes[0x17] = S_KEY_5;
    keycodes[0x16] = S_KEY_6;
    keycodes[0x1A] = S_KEY_7;
    keycodes[0x1C] = S_KEY_8;
    keycodes[0x19] = S_KEY_9;
    keycodes[0x00] = S_KEY_A;
    keycodes[0x0B] = S_KEY_B;
    keycodes[0x08] = S_KEY_C;
    keycodes[0x02] = S_KEY_D;
    keycodes[0x0E] = S_KEY_E;
    keycodes[0x03] = S_KEY_F;
    keycodes[0x05] = S_KEY_G;
    keycodes[0x04] = S_KEY_H;
    keycodes[0x22] = S_KEY_I;
    keycodes[0x26] = S_KEY_J;
    keycodes[0x28] = S_KEY_K;
    keycodes[0x25] = S_KEY_L;
    keycodes[0x2E] = S_KEY_M;
    keycodes[0x2D] = S_KEY_N;
    keycodes[0x1F] = S_KEY_O;
    keycodes[0x23] = S_KEY_P;
    keycodes[0x0C] = S_KEY_Q;
    keycodes[0x0F] = S_KEY_R;
    keycodes[0x01] = S_KEY_S;
    keycodes[0x11] = S_KEY_T;
    keycodes[0x20] = S_KEY_U;
    keycodes[0x09] = S_KEY_V;
    keycodes[0x0D] = S_KEY_W;
    keycodes[0x07] = S_KEY_X;
    keycodes[0x10] = S_KEY_Y;
    keycodes[0x06] = S_KEY_Z;

    keycodes[0x27] = S_KEY_APOSTROPHE;
    keycodes[0x2A] = S_KEY_BACKSLASH;
    keycodes[0x2B] = S_KEY_COMMA;
    keycodes[0x18] = S_KEY_EQUAL;
    keycodes[0x32] = S_KEY_GRAVE_ACCENT;
    keycodes[0x21] = S_KEY_LEFT_BRACKET;
    keycodes[0x1B] = S_KEY_MINUS;
    keycodes[0x2F] = S_KEY_PERIOD;
    keycodes[0x1E] = S_KEY_RIGHT_BRACKET;
    keycodes[0x29] = S_KEY_SEMICOLON;
    keycodes[0x2C] = S_KEY_SLASH;
    keycodes[0x0A] = S_KEY_WORLD_1;

    keycodes[0x33] = S_KEY_BACKSPACE;
    keycodes[0x39] = S_KEY_CAPS_LOCK;
    keycodes[0x75] = S_KEY_DELETE;
    keycodes[0x7D] = S_KEY_DOWN;
    keycodes[0x77] = S_KEY_END;
    keycodes[0x24] = S_KEY_ENTER;
    keycodes[0x35] = S_KEY_ESCAPE;
    keycodes[0x7A] = S_KEY_F1;
    keycodes[0x78] = S_KEY_F2;
    keycodes[0x63] = S_KEY_F3;
    keycodes[0x76] = S_KEY_F4;
    keycodes[0x60] = S_KEY_F5;
    keycodes[0x61] = S_KEY_F6;
    keycodes[0x62] = S_KEY_F7;
    keycodes[0x64] = S_KEY_F8;
    keycodes[0x65] = S_KEY_F9;
    keycodes[0x6D] = S_KEY_F10;
    keycodes[0x67] = S_KEY_F11;
    keycodes[0x6F] = S_KEY_F12;
    keycodes[0x69] = S_KEY_PRINT_SCREEN;
    keycodes[0x6B] = S_KEY_F14;
    keycodes[0x71] = S_KEY_F15;
    keycodes[0x6A] = S_KEY_F16;
    keycodes[0x40] = S_KEY_F17;
    keycodes[0x4F] = S_KEY_F18;
    keycodes[0x50] = S_KEY_F19;
    keycodes[0x5A] = S_KEY_F20;
    keycodes[0x73] = S_KEY_HOME;
    keycodes[0x72] = S_KEY_INSERT;
    keycodes[0x7B] = S_KEY_LEFT;
    keycodes[0x3A] = S_KEY_LEFT_ALT;
    keycodes[0x3B] = S_KEY_LEFT_CONTROL;
    keycodes[0x38] = S_KEY_LEFT_SHIFT;
    keycodes[0x37] = S_KEY_LEFT_SUPER;
    keycodes[0x6E] = S_KEY_MENU;
    keycodes[0x47] = S_KEY_NUM_LOCK;
    keycodes[0x79] = S_KEY_PAGE_DOWN;
    keycodes[0x74] = S_KEY_PAGE_UP;
    keycodes[0x7C] = S_KEY_RIGHT;
    keycodes[0x3D] = S_KEY_RIGHT_ALT;
    keycodes[0x3E] = S_KEY_RIGHT_CONTROL;
    keycodes[0x3C] = S_KEY_RIGHT_SHIFT;
    keycodes[0x36] = S_KEY_RIGHT_SUPER;
    keycodes[0x31] = S_KEY_SPACE;
    keycodes[0x30] = S_KEY_TAB;
    keycodes[0x7E] = S_KEY_UP;

    keycodes[0x52] = S_KEY_KP_0;
    keycodes[0x53] = S_KEY_KP_1;
    keycodes[0x54] = S_KEY_KP_2;
    keycodes[0x55] = S_KEY_KP_3;
    keycodes[0x56] = S_KEY_KP_4;
    keycodes[0x57] = S_KEY_KP_5;
    keycodes[0x58] = S_KEY_KP_6;
    keycodes[0x59] = S_KEY_KP_7;
    keycodes[0x5B] = S_KEY_KP_8;
    keycodes[0x5C] = S_KEY_KP_9;
    keycodes[0x45] = S_KEY_KP_ADD;
    keycodes[0x41] = S_KEY_KP_DECIMAL;
    keycodes[0x4B] = S_KEY_KP_DIVIDE;
    keycodes[0x4C] = S_KEY_KP_ENTER;
    keycodes[0x51] = S_KEY_KP_EQUAL;
    keycodes[0x43] = S_KEY_KP_MULTIPLY;
    keycodes[0x4E] = S_KEY_KP_SUBTRACT;
}

@end
