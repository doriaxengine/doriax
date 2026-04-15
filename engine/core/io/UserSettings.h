//
// (c) 2026 Eduardo Doria.
//

#ifndef USERSETTINGS_H
#define USERSETTINGS_H

#include "Export.h"
#include <string>
#include "io/Data.h"

namespace doriax {
    class DORIAX_API UserSettings {
    public:

        static bool getBoolForKey(const char *key);
        static int getIntegerForKey(const char *key);
        static long getLongForKey(const char *key);
        static float getFloatForKey(const char *key);
        static double getDoubleForKey(const char *key);
        static std::string getStringForKey(const char *key);
        static Data getDataForKey(const char *key);

        static bool getBoolForKey(const char *key, bool defaultValue);
        static int getIntegerForKey(const char *key, int defaultValue);
        static long getLongForKey(const char *key, long defaultValue);
        static float getFloatForKey(const char *key, float defaultValue);
        static double getDoubleForKey(const char *key, double defaultValue);
        static std::string getStringForKey(const char *key, const std::string& defaultValue);
        static Data getDataForKey(const char *key, const Data& defaultValue);

        static void setBoolForKey(const char *key, bool value);
        static void setIntegerForKey(const char *key, int value);
        static void setLongForKey(const char *key, long value);
        static void setFloatForKey(const char *key, float value);
        static void setDoubleForKey(const char *key, double value);
        static void setStringForKey(const char* key, const std::string& value);
        static void setDataForKey(const char *key, Data& value);

        static void removeKey(const char *key);

    };
}


#endif //USERSETTINGS_H
