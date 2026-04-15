package org.doriaxengine.doriax;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;

public class UserSettings {
    private Activity activity;

    private static final String PREFS_FILE_NAME = "UserSettings";

    public UserSettings(final Activity activity) {
        this.activity = activity;
    }

    public boolean getBoolForKey(String key, boolean defaultValue) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return sharedPref.getBoolean(key, defaultValue);
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public int getIntegerForKey(String key, int defaultValue) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return sharedPref.getInt(key, defaultValue);
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public long getLongForKey(String key, long defaultValue) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return sharedPref.getLong(key, defaultValue);
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public float getFloatForKey(String key, float defaultValue) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return sharedPref.getFloat(key, defaultValue);
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public double getDoubleForKey(String key, double defaultValue) {
        // SharedPreferences doesn't support double, using long to store
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return Double.longBitsToDouble(sharedPref.getLong(key, Double.doubleToRawLongBits(defaultValue)));
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public String getStringForKey(String key, String defaultValue) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);

        try {
            return sharedPref.getString(key, defaultValue);
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        return defaultValue;
    }

    public void setBoolForKey(String key, boolean value) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putBoolean(key, value);
        editor.apply();
    }

    public void setIntegerForKey(String key, int value) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putInt(key, value);
        editor.apply();
    }

    public void setLongForKey(String key, long value) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putLong(key, value);
        editor.apply();
    }

    public void setFloatForKey(String key, float value) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putFloat(key, value);
        editor.apply();
    }

    public void setDoubleForKey(String key, double value) {
        // SharedPreferences doesn't support double, using long to store
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putLong(key, Double.doubleToRawLongBits(value));
        editor.apply();
    }

    public void setStringForKey(String key, String value) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.putString(key, value);
        editor.apply();
    }

    public void removeKey(String key) {
        SharedPreferences sharedPref = activity.getSharedPreferences(PREFS_FILE_NAME, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = sharedPref.edit();
        editor.remove(key);
        editor.apply();
    }
}
