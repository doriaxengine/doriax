//
// (c) 2026 Eduardo Doria.
//

#include "System.h"
#include "util/XMLUtils.h"
#include "util/Base64.h"
#include "Log.h"
#include <stdlib.h>

using namespace doriax;

System* System::systemInstance = nullptr;

#define USERSETTINGS_ROOT "userSettings"
#define USERSETTINGS_XML_FILE "data://UserSettings.xml"

System& System::instance(){
    if (!systemInstance) {
        // Need to be printf here, Log class uses instance()
        printf("System instance not set. Engine must call System::setSystemInstance() during initialization.\n");
        abort();
    }

    return *systemInstance;
}

void System::setSystemInstance(System* system){
    systemInstance = system;
}

sg_environment System::getSokolEnvironment(){
    sg_environment env = {};

    env.defaults.sample_count = getSampleCount();
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;

    return env;
}

sg_swapchain System::getSokolSwapchain(){
    sg_swapchain swapchain = { 0 };

    swapchain.width = getScreenWidth();
    swapchain.height = getScreenHeight();
    swapchain.sample_count = getSampleCount();
    swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;

    // we just assume here that the GL framebuffer is always 0
    swapchain.gl.framebuffer = 0;

    return swapchain;
}

void System::setMouseCursor(CursorType type){

}

void System::setShowCursor(bool showCursor){

}

int System::getSampleCount(){
    return 1;
}

void System::showVirtualKeyboard(std::wstring text){

}

void System::hideVirtualKeyboard(){

}

bool System::isFullscreen(){
    return false;
}

void System::requestFullscreen(){

}

void System::exitFullscreen(){

}

char System::getDirSeparator(){
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

std::string System::getAssetPath(){
    return "";
}

std::string System::getUserDataPath(){
    return getAssetPath();
}

std::string System::getLuaPath(){
    return getAssetPath();
}

std::string System::getShaderPath(){
    return getAssetPath() + "/" + "shaders";
}

FILE* System::platformFopen(const char* fname, const char* mode){
    return fopen(fname, mode);
}

bool System::syncFileSystem(){
    return true;
}

void System::platformLog(const int type, const char *fmt, va_list args){
    const char* priority;

    if (type == S_LOG_VERBOSE){
        priority = "VERBOSE";
    }else if (type == S_LOG_DEBUG){
        priority = "DEBUG";
    }else if (type == S_LOG_WARN){
        priority = "WARN";
    }else if (type == S_LOG_ERROR){
        priority = "ERROR";
    }

    if ((type == S_LOG_VERBOSE) || (type == S_LOG_DEBUG) || (type == S_LOG_WARN) || (type == S_LOG_ERROR))
        printf("(%s): ", priority);

    vprintf(fmt, args);
    printf("\n");
}

bool System::getBoolForKey(const char *key, bool defaultValue){
    return getStringForKey(key, defaultValue ? "true" : "false") == "true";
}

int System::getIntegerForKey(const char *key, int defaultValue){
    return std::stoi(getStringForKey(key, std::to_string(defaultValue).c_str()));
}

long System::getLongForKey(const char *key, long defaultValue){
    return std::stol(getStringForKey(key, std::to_string(defaultValue).c_str()));
}

float System::getFloatForKey(const char *key, float defaultValue){
    return std::stof(getStringForKey(key, std::to_string(defaultValue).c_str()));
}

double System::getDoubleForKey(const char *key, double defaultValue){
    return std::stod(getStringForKey(key, std::to_string(defaultValue).c_str()));
}

Data System::getDataForKey(const char *key, const Data& defaultValue){
    std::string ret = System::instance().getStringForKey(key, "");

    if (ret.empty())
        return defaultValue;
    
    std::vector<unsigned char> decodedData = Base64::decode(ret);

    return Data(&decodedData[0], (unsigned int)decodedData.size(), true, true);
}

std::string System::getStringForKey(const char *key, const std::string& defaultValue){
    std::string value = XMLUtils::getValueForKey(USERSETTINGS_XML_FILE, USERSETTINGS_ROOT, key);

    if (value.empty())
        return defaultValue;

    return value;
}

void System::setBoolForKey(const char *key, bool value){
    setStringForKey(key, value ? "true" : "false");
}

void System::setIntegerForKey(const char *key, int value){
    setStringForKey(key, std::to_string(value).c_str());
}

void System::setLongForKey(const char *key, long value){
    setStringForKey(key, std::to_string(value).c_str());
}

void System::setFloatForKey(const char *key, float value){
    setStringForKey(key, std::to_string(value).c_str());
}

void System::setDoubleForKey(const char *key, double value){
    setStringForKey(key, std::to_string(value).c_str());
}

void System::setDataForKey(const char *key, Data& value){
    if (value.getMemPtr()) {
        System::instance().setStringForKey(key, Base64::encode(value.getMemPtr(), value.length()));
    }else{
        Log::error("No data to add for key: %s", key);
    }
}

void System::setStringForKey(const char* key, const std::string& value){
    XMLUtils::setValueForKey(USERSETTINGS_XML_FILE, USERSETTINGS_ROOT, key, value.c_str());
}

void System::removeKey(const char *key){
    XMLUtils::removeKey(USERSETTINGS_XML_FILE, USERSETTINGS_ROOT, key);
}

void System::initializeAdMob(bool tagForChildDirectedTreatment, bool tagForUnderAgeOfConsent){
    Log::error("Cannot initialize AdMob in this system");
}

void System::setMaxAdContentRating(AdMobRating rating){
    Log::error("Cannot set AdMob rating in this system");
}

void System::loadInterstitialAd(const std::string& adUnitID){
    Log::error("Cannot load InterstitialAd in this system");
}

bool System::isInterstitialAdLoaded(){
    return false;
}

void System::showInterstitialAd(){
    Log::error("Cannot show InterstitialAd in this system");
}

void System::initializeCrazyGamesSDK(){
    Log::error("Cannot initialize CrazyGames SDK in this system");
}

void System::showCrazyGamesAd(const std::string& type){
    Log::error("Cannot show CrazyGames ad in this system");
}

void System::happytimeCrazyGames(){
}

void System::gameplayStartCrazyGames(){
}

void System::gameplayStopCrazyGames(){
}

void System::loadingStartCrazyGames(){
}

void System::loadingStopCrazyGames(){
}
