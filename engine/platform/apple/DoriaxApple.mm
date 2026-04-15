#include "DoriaxApple.h"

#import "Renderer.h"
#import <Foundation/Foundation.h>

#if TARGET_OS_IPHONE
#import <MetalKit/MetalKit.h>

#import "ios/AdMobAdapter.h"

static AdMobAdapter* admob = nil;
#endif

DoriaxApple::DoriaxApple(){
#if TARGET_OS_IPHONE
    if (!admob)
        admob = [[AdMobAdapter alloc]init];
#endif
}

DoriaxApple::~DoriaxApple(){
#if TARGET_OS_IPHONE
    admob = nil;
#endif
}

sg_environment DoriaxApple::getSokolEnvironment(){
    return (sg_environment) {
        .defaults = {
            .sample_count = (int)Renderer.view.sampleCount,
            .color_format = SG_PIXELFORMAT_BGRA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        },
        .metal = {
            .device = (__bridge const void*) Renderer.view.device,
        }
    };
}

sg_swapchain DoriaxApple::getSokolSwapchain(){
    return (sg_swapchain) {
        .width = (int)Renderer.screenSize.width,
        .height = (int)Renderer.screenSize.height,
        .sample_count = (int)Renderer.view.sampleCount,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .metal = {
            .current_drawable = (__bridge const void*) [Renderer.view currentDrawable],
            .depth_stencil_texture = (__bridge const void*) [Renderer.view depthStencilTexture],
            .msaa_color_texture = (__bridge const void*) [Renderer.view multisampleColorTexture],
        }
    };
}

#if defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE
// undocumented methods for creating cursors (see GLFW 3.4 and imgui_impl_osx.mm)
@interface NSCursor()
+ (id)_windowResizeNorthWestSouthEastCursor;
+ (id)_windowResizeNorthEastSouthWestCursor;
+ (id)_windowResizeNorthSouthCursor;
+ (id)_windowResizeEastWestCursor;
@end
#endif

void DoriaxApple::setMouseCursor(doriax::CursorType type){
#if defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE
    NSCursor* cursor;
    if (type == doriax::CursorType::ARROW){
        cursor = [NSCursor arrowCursor];
    }else if (type == doriax::CursorType::IBEAM){
        cursor = [NSCursor IBeamCursor];
    }else if (type == doriax::CursorType::CROSSHAIR){
        cursor = [NSCursor crosshairCursor];
    }else if (type == doriax::CursorType::POINTING_HAND){
        cursor = [NSCursor pointingHandCursor];
    }else if (type == doriax::CursorType::RESIZE_EW){
        cursor = [NSCursor respondsToSelector:@selector(_windowResizeEastWestCursor)] ? [NSCursor _windowResizeEastWestCursor] : [NSCursor resizeLeftRightCursor];
    }else if (type == doriax::CursorType::RESIZE_NS){
        cursor = [NSCursor respondsToSelector:@selector(_windowResizeNorthSouthCursor)] ? [NSCursor _windowResizeNorthSouthCursor] : [NSCursor resizeUpDownCursor];
    }else if (type == doriax::CursorType::RESIZE_NWSE){
        cursor = [NSCursor respondsToSelector:@selector(_windowResizeNorthWestSouthEastCursor)] ? [NSCursor _windowResizeNorthWestSouthEastCursor] : [NSCursor closedHandCursor];
    }else if (type == doriax::CursorType::RESIZE_NESW){
        cursor = [NSCursor respondsToSelector:@selector(_windowResizeNorthEastSouthWestCursor)] ? [NSCursor _windowResizeNorthEastSouthWestCursor] : [NSCursor closedHandCursor];
    }else if (type == doriax::CursorType::RESIZE_ALL){
        cursor = [NSCursor closedHandCursor];
    }else if (type == doriax::CursorType::NOT_ALLOWED){
        cursor = [NSCursor operationNotAllowedCursor];
    }

    [cursor set];
#endif
}

void DoriaxApple::setShowCursor(bool showCursor){
#if defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE
    if (!showCursor){
        [NSCursor hide];
    }else{
        [NSCursor unhide];
    }
#endif
}

int DoriaxApple::getScreenWidth(){
    return Renderer.screenSize.width;
}

int DoriaxApple::getScreenHeight(){
    return Renderer.screenSize.height;
}

int DoriaxApple::getSampleCount(){
    return (int)Renderer.view.sampleCount;
}

void DoriaxApple::showVirtualKeyboard(std::wstring text){
#if TARGET_OS_IPHONE
    [Renderer.view becomeFirstResponder];
#endif
}

void DoriaxApple::hideVirtualKeyboard(){
#if TARGET_OS_IPHONE
    [Renderer.view resignFirstResponder];
#endif
}

std::string DoriaxApple::getAssetPath(){
    NSMutableString* adjusted_relative_path = [[NSMutableString alloc] initWithString:@"assets"];
    
    NSString* path = [[NSBundle mainBundle] pathForResource:adjusted_relative_path ofType:nil];
    
    if (!path)
        return "";
    
    return [path cStringUsingEncoding:NSASCIIStringEncoding];
}

std::string DoriaxApple::getUserDataPath(){
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [paths objectAtIndex:0];
    
    return [documentsDirectory UTF8String];
}

std::string DoriaxApple::getLuaPath(){
    NSMutableString* adjusted_relative_path = [[NSMutableString alloc] initWithString:@"lua"];
    
    NSString* path = [[NSBundle mainBundle] pathForResource:adjusted_relative_path ofType:nil];
    
    if (!path)
        return "";
    
    return [path cStringUsingEncoding:NSASCIIStringEncoding];
}

bool DoriaxApple::getBoolForKey(const char *key, bool defaultValue){
    NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:[NSString stringWithUTF8String:key]];
    if (value) {
        return [value boolValue];
    }

    return defaultValue;
}

int DoriaxApple::getIntegerForKey(const char *key, int defaultValue){
    NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:[NSString stringWithUTF8String:key]];
    if (value) {
        return [value intValue];
    }

    return defaultValue;
}

long DoriaxApple::getLongForKey(const char *key, long defaultValue){
    NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:[NSString stringWithUTF8String:key]];
    if (value) {
        return [value longValue];
    }

    return defaultValue;
}

float DoriaxApple::getFloatForKey(const char *key, float defaultValue){
    NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:[NSString stringWithUTF8String:key]];
    if (value) {
        return [value floatValue];
    }

    return defaultValue;
}

double DoriaxApple::getDoubleForKey(const char *key, double defaultValue){
    NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:[NSString stringWithUTF8String:key]];
    if (value) {
        return [value doubleValue];
    }

    return defaultValue;
}

doriax::Data DoriaxApple::getDataForKey(const char* key, const doriax::Data& defaultValue){
    NSData *data = [[NSUserDefaults standardUserDefaults] dataForKey:[NSString stringWithUTF8String:key]];
    if (data){
        return doriax::Data((unsigned char*)data.bytes, (unsigned int)data.length, true, true);
    }
    
     return defaultValue;
}

std::string DoriaxApple::getStringForKey(const char *key, const std::string& defaultValue){
    NSString *str = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithUTF8String:key]];
    if (str){
        return [str UTF8String];
    }
    
    return defaultValue;
}

void DoriaxApple::setBoolForKey(const char *key, bool value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSNumber numberWithBool:value] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setIntegerForKey(const char *key, int value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSNumber numberWithInt:value] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setLongForKey(const char *key, long value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSNumber numberWithLong:value] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setFloatForKey(const char *key, float value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSNumber numberWithFloat:value] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setDoubleForKey(const char *key, double value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSNumber numberWithDouble:value] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setDataForKey(const char* key, doriax::Data& value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSData dataWithBytes: value.getMemPtr() length: value.length()] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::setStringForKey(const char* key, const std::string& value){
    [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:value.c_str()] forKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::removeKey(const char *key){
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:[NSString stringWithUTF8String:key]];
}

void DoriaxApple::initializeAdMob(bool tagForChildDirectedTreatment, bool tagForUnderAgeOfConsent){
#if TARGET_OS_IPHONE
    [admob initializeAdMob: tagForChildDirectedTreatment and:tagForUnderAgeOfConsent];
#endif
}

void DoriaxApple::setMaxAdContentRating(doriax::AdMobRating rating){
    int irating = 0;
    if (rating == doriax::AdMobRating::General){
        irating = 1;
    }else if (rating == doriax::AdMobRating::ParentalGuidance){
        irating = 2;
    }else if (rating == doriax::AdMobRating::Teen){
        irating = 3;
    }else if (rating == doriax::AdMobRating::MatureAudience){
        irating = 4;
    }
#if TARGET_OS_IPHONE
    [admob setMaxAdContentRating: irating];
#endif
}

void DoriaxApple::loadInterstitialAd(const std::string& adUnitID){
#if TARGET_OS_IPHONE
    [admob loadInterstitial:[NSString stringWithUTF8String:adUnitID.c_str()]];
#endif
}

bool DoriaxApple::isInterstitialAdLoaded(){
#if TARGET_OS_IPHONE
    return [admob isInterstitialAdLoaded];
#else
    return false;
#endif
}

void DoriaxApple::showInterstitialAd(){
#if TARGET_OS_IPHONE
    [admob showInterstitial];
#endif
}
