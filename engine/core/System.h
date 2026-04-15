//
// (c) 2026 Eduardo Doria.
//

#ifndef SYSTEM_H
#define SYSTEM_H

#define S_LOG_VERBOSE 1
#define S_LOG_DEBUG 2
#define S_LOG_WARN 3
#define S_LOG_ERROR 4

#include "Export.h"
#include <stdio.h>
#include <string>
#include <vector>
#include "io/Data.h"
#include "sokol_gfx.h"

namespace doriax {
    class Engine;

    enum class AdMobRating{
        General,
        ParentalGuidance,
        Teen,
        MatureAudience
    };

    enum class CursorType{
        ARROW, // default
        IBEAM,
        CROSSHAIR,
        POINTING_HAND,
        RESIZE_EW,
        RESIZE_NS,
        RESIZE_NWSE,
        RESIZE_NESW,
        RESIZE_ALL,
        NOT_ALLOWED
    };

    class DORIAX_API System {
    private:

        friend class Engine;

        static System* systemInstance;

        // Only Engine is allowed to inject an external System implementation.
        static void setSystemInstance(System* system);

    protected:

        System() {}

    public:
        std::vector<std::string> args;

        static System& instance();

        virtual ~System() {}

        // *******
        // Used for user and Lua
        // *******
        virtual int getScreenWidth() = 0;
        virtual int getScreenHeight() = 0;

        virtual int getSampleCount();

        virtual void showVirtualKeyboard(std::wstring text = L"");
        virtual void hideVirtualKeyboard();

        virtual bool isFullscreen();
        virtual void requestFullscreen();
        virtual void exitFullscreen();

        virtual char getDirSeparator();
        
        virtual std::string getAssetPath();
        virtual std::string getUserDataPath();
        virtual std::string getLuaPath();
        virtual std::string getShaderPath();

        // *******
        // Used only for engine
        // *******
        virtual sg_environment getSokolEnvironment();
        virtual sg_swapchain getSokolSwapchain();

        virtual void setMouseCursor(CursorType type);
        virtual void setShowCursor(bool showCursor);

        virtual FILE* platformFopen(const char* fname, const char* mode);
        virtual bool syncFileSystem();

        virtual void platformLog(const int type, const char *fmt, va_list args);

        virtual bool getBoolForKey(const char *key, bool defaultValue);
        virtual int getIntegerForKey(const char *key, int defaultValue);
        virtual long getLongForKey(const char *key, long defaultValue);
        virtual float getFloatForKey(const char *key, float defaultValue);
        virtual double getDoubleForKey(const char *key, double defaultValue);
        virtual Data getDataForKey(const char *key, const Data& defaultValue);
        virtual std::string getStringForKey(const char *key, const std::string& defaultValue);

        virtual void setBoolForKey(const char *key, bool value);
        virtual void setIntegerForKey(const char *key, int value);
        virtual void setLongForKey(const char *key, long value);
        virtual void setFloatForKey(const char *key, float value);
        virtual void setDoubleForKey(const char *key, double value);
        virtual void setDataForKey(const char *key, Data& value);
        virtual void setStringForKey(const char* key, const std::string& value);

        virtual void removeKey(const char *key);

        // Google AdMob SDK
        virtual void initializeAdMob(bool tagForChildDirectedTreatment = false, bool tagForUnderAgeOfConsent = false);
        virtual void setMaxAdContentRating(AdMobRating rating);
        virtual void loadInterstitialAd(const std::string& adUnitID);
        virtual bool isInterstitialAdLoaded();
        virtual void showInterstitialAd();

        // CrazyGames SDK
        virtual void initializeCrazyGamesSDK();
        virtual void showCrazyGamesAd(const std::string& type);
        virtual void happytimeCrazyGames();
        virtual void gameplayStartCrazyGames();
        virtual void gameplayStopCrazyGames();
        virtual void loadingStartCrazyGames();
        virtual void loadingStopCrazyGames();
    };

}


#endif //SYSTEM_H
