#ifndef DoriaxApple_h
#define DoriaxApple_h

#include "System.h"

class DoriaxApple: public doriax::System{

public:

    DoriaxApple();
    
    virtual ~DoriaxApple();
    
    virtual sg_environment getSokolEnvironment();
    virtual sg_swapchain getSokolSwapchain();

    virtual void setMouseCursor(doriax::CursorType type);
    virtual void setShowCursor(bool showCursor);

    virtual int getScreenWidth();
    virtual int getScreenHeight();
    
    virtual int getSampleCount();

    virtual void showVirtualKeyboard(std::wstring text);
    virtual void hideVirtualKeyboard();
    
    virtual std::string getAssetPath();
    virtual std::string getUserDataPath();
    virtual std::string getLuaPath();
    
    virtual bool getBoolForKey(const char *key, bool defaultValue);
    virtual int getIntegerForKey(const char *key, int defaultValue);
    virtual long getLongForKey(const char *key, long defaultValue);
    virtual float getFloatForKey(const char *key, float defaultValue);
    virtual double getDoubleForKey(const char *key, double defaultValue);
    virtual doriax::Data getDataForKey(const char* key, const doriax::Data& defaultValue);
    virtual std::string getStringForKey(const char *key, const std::string& defaultValue);

    virtual void setBoolForKey(const char *key, bool value);
    virtual void setIntegerForKey(const char *key, int value);
    virtual void setLongForKey(const char *key, long value);
    virtual void setFloatForKey(const char *key, float value);
    virtual void setDoubleForKey(const char *key, double value);
    virtual void setDataForKey(const char* key, doriax::Data& value);
    virtual void setStringForKey(const char* key, const std::string& value);

    virtual void removeKey(const char *key);

    virtual void initializeAdMob(bool tagForChildDirectedTreatment, bool tagForUnderAgeOfConsent);
    virtual void setMaxAdContentRating(doriax::AdMobRating rating);
    virtual void loadInterstitialAd(const std::string& adUnitID);
    virtual bool isInterstitialAdLoaded();
    virtual void showInterstitialAd();
};


#endif /* DoriaxSokol_h */
