//
// (c) 2026 Eduardo Doria.
//

#include "UserSettings.h"
#include "System.h"

using namespace doriax;

bool UserSettings::getBoolForKey(const char *key){
    return getBoolForKey(key, false);
}

int UserSettings::getIntegerForKey(const char *key){
    return getIntegerForKey(key, 0);
}

long UserSettings::getLongForKey(const char *key){
    return getLongForKey(key, 0);
}

float UserSettings::getFloatForKey(const char *key){
    return getFloatForKey(key, 0);
}

double UserSettings::getDoubleForKey(const char *key){
    return getDoubleForKey(key, 0);
}

std::string UserSettings::getStringForKey(const char *key){
    return getStringForKey(key, "");
}

Data UserSettings::getDataForKey(const char *key){
    return getDataForKey(key, Data());
}

bool UserSettings::getBoolForKey(const char *key, bool defaultValue){
    return System::instance().getBoolForKey(key, defaultValue);
}

int UserSettings::getIntegerForKey(const char *key, int defaultValue){
    return System::instance().getIntegerForKey(key, defaultValue);
}

long UserSettings::getLongForKey(const char *key, long defaultValue){
    return System::instance().getLongForKey(key, defaultValue);
}

float UserSettings::getFloatForKey(const char *key, float defaultValue){
    return System::instance().getFloatForKey(key, defaultValue);
}

double UserSettings::getDoubleForKey(const char *key, double defaultValue){
    return System::instance().getDoubleForKey(key, defaultValue);
}

std::string UserSettings::getStringForKey(const char *key, const std::string& defaultValue){
    return System::instance().getStringForKey(key, defaultValue);
}

Data UserSettings::getDataForKey(const char *key, const Data& defaultValue){
    return System::instance().getDataForKey(key, defaultValue);
}

void UserSettings::setBoolForKey(const char *key, bool value){
    System::instance().setBoolForKey(key, value);
}

void UserSettings::setIntegerForKey(const char *key, int value){
    System::instance().setIntegerForKey(key, value);
}

void UserSettings::setLongForKey(const char *key, long value){
    System::instance().setLongForKey(key, value);
}

void UserSettings::setFloatForKey(const char *key, float value){
    System::instance().setFloatForKey(key, value);
}

void UserSettings::setDoubleForKey(const char *key, double value){
    System::instance().setDoubleForKey(key, value);
}

void UserSettings::setStringForKey(const char* key, const std::string& value){
    System::instance().setStringForKey(key, value);
}

void UserSettings::setDataForKey(const char *key, Data& value){
    System::instance().setDataForKey(key, value);
}

void UserSettings::removeKey(const char *key){
    System::instance().removeKey(key);
}
