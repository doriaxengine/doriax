#ifndef DoriaxAndroid_H_
#define DoriaxAndroid_H_

#include <stdio.h>
#include <string>
#include "System.h"

class DoriaxAndroid: public doriax::System {

private:

    const char* logtag;

public:

    DoriaxAndroid();

    virtual int getScreenWidth();
    virtual int getScreenHeight();

	virtual void showVirtualKeyboard(std::wstring text);
	virtual void hideVirtualKeyboard();

	std::string getUserDataPath();

    virtual FILE* platformFopen(const char* fname, const char* mode);
	virtual void platformLog(const int type, const char *fmt, va_list args);

	virtual bool getBoolForKey(const char *key, bool defaultValue);
	virtual int getIntegerForKey(const char *key, int defaultValue);
	virtual long getLongForKey(const char *key, long defaultValue);
	virtual float getFloatForKey(const char *key, float defaultValue);
	virtual double getDoubleForKey(const char *key, double defaultValue);
	virtual std::string getStringForKey(const char *key, const std::string& defaultValue);

	virtual void setBoolForKey(const char *key, bool value);
	virtual void setIntegerForKey(const char *key, int value);
	virtual void setLongForKey(const char *key, long value);
	virtual void setFloatForKey(const char *key, float value);
	virtual void setDoubleForKey(const char *key, double value);
	virtual void setStringForKey(const char* key, const std::string& value);

	virtual void removeKey(const char* key);

	virtual void initializeAdMob(bool tagForChildDirectedTreatment, bool tagForUnderAgeOfConsent);
    virtual void setMaxAdContentRating(doriax::AdMobRating rating);
	virtual void loadInterstitialAd(const std::string& adUnitID);
	virtual bool isInterstitialAdLoaded();
	virtual void showInterstitialAd();
};

#endif /* DoriaxAndroid_H_ */
