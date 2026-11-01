// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

// DoriaxAndroid's AdMob and Google Play Billing backends, and the natives of their Java wrappers

#include "AndroidServices.h"

#include "DoriaxAndroid.h"
#include "NativeEngine.h"
#include "service/AdMob.h"
#include "service/InAppPurchase.h"
#include "Log.h"
#include "util/StringUtils.h"

#include <cstdint>

using namespace doriax;

namespace {

    // A pending Java exception would abort the next JNI call
    bool clearException(JNIEnv* env){
        if (env->ExceptionCheck()){
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        }
        return false;
    }

    // Java strings are UTF-16; GetStringUTFChars gives modified UTF-8, which breaks emoji
    std::string toString(JNIEnv* env, jstring text){
        if (!text)
            return "";

        const jsize length = env->GetStringLength(text);
        const jchar* chars = env->GetStringChars(text, nullptr);
        if (!chars)
            return "";

        std::string utf8;
        for (jsize i = 0; i < length; i++){
            uint32_t code = chars[i];
            if (code >= 0xD800 && code <= 0xDBFF && i + 1 < length && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF){
                code = 0x10000 + ((code - 0xD800) << 10) + (chars[++i] - 0xDC00);
            }
            utf8 += StringUtils::toUTF8(code);
        }
        env->ReleaseStringChars(text, chars);

        return utf8;
    }

    // NewStringUTF takes modified UTF-8 and rejects 4-byte sequences, so build UTF-16
    jstring toJavaString(JNIEnv* env, const std::string& text){
        bool hadInvalid;
        std::u16string utf16;
        for (uint32_t code : StringUtils::decodeUtf8ToCodepoints(text, hadInvalid)){
            if (code >= 0x10000){
                code -= 0x10000;
                utf16 += static_cast<char16_t>(0xD800 + (code >> 10));
                utf16 += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
            }else{
                utf16 += static_cast<char16_t>(code);
            }
        }
        return env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
    }

    class JavaString{
    public:
        JavaString(JNIEnv* env, const std::string& text): env(env), value(toJavaString(env, text)) {}
        ~JavaString(){ if (value) env->DeleteLocalRef(value); }
        JavaString(const JavaString&) = delete;
        JavaString& operator=(const JavaString&) = delete;

        jstring get() const { return value; }

    private:
        JNIEnv* env;
        jstring value;
    };

    class JavaStringArray{
    public:
        JavaStringArray(JNIEnv* env, const std::vector<std::string>& values): env(env) {
            jclass stringClass = env->FindClass("java/lang/String");
            value = env->NewObjectArray(static_cast<jsize>(values.size()), stringClass, nullptr);
            for (size_t i = 0; value && i < values.size(); i++){
                jstring item = toJavaString(env, values[i]);
                env->SetObjectArrayElement(value, static_cast<jsize>(i), item);
                env->DeleteLocalRef(item);
            }
            env->DeleteLocalRef(stringClass);
        }
        ~JavaStringArray(){ if (value) env->DeleteLocalRef(value); }
        JavaStringArray(const JavaStringArray&) = delete;
        JavaStringArray& operator=(const JavaStringArray&) = delete;

        jobjectArray get() const { return value; }

    private:
        JNIEnv* env;
        jobjectArray value = nullptr;
    };

    JNIEnv* jniEnv(){
        return NativeEngine::getInstance()->getJniEnv();
    }

    template<typename... Args>
    void callVoid(jobject wrapper, jmethodID method, Args... args){
        JNIEnv* env = jniEnv();
        env->CallVoidMethod(wrapper, method, args...);
        clearException(env);
    }

    bool callBoolean(jobject wrapper, jmethodID method){
        JNIEnv* env = jniEnv();
        const jboolean value = env->CallBooleanMethod(wrapper, method);
        return !clearException(env) && value;
    }

    // AdMobWrapper natives

    void JNICALL admobOnInitialized(JNIEnv* env, jclass cls){
        AdMob::systemInitialized();
    }

    void JNICALL admobOnConsentUpdated(JNIEnv* env, jclass cls, jint errorCode, jstring message){
        AdMob::systemConsentUpdated(errorCode, toString(env, message));
    }

    void JNICALL admobOnAdLoaded(JNIEnv* env, jclass cls, jint format, jint generation, jint width, jint height){
        AdMob::systemAdLoaded(static_cast<AdMobFormat>(format), generation, width, height);
    }

    void JNICALL admobOnAdFailedToLoad(JNIEnv* env, jclass cls, jint format, jint generation, jint errorCode, jstring message){
        AdMob::systemAdFailedToLoad(static_cast<AdMobFormat>(format), generation, errorCode, toString(env, message));
    }

    void JNICALL admobOnAdShown(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdShown(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdFailedToShow(JNIEnv* env, jclass cls, jint format, jint errorCode, jstring message){
        AdMob::systemAdFailedToShow(static_cast<AdMobFormat>(format), errorCode, toString(env, message));
    }

    void JNICALL admobOnAdDismissed(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdDismissed(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdClicked(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdClicked(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdImpression(JNIEnv* env, jclass cls, jint format){
        AdMob::systemAdImpression(static_cast<AdMobFormat>(format));
    }

    void JNICALL admobOnAdPaid(JNIEnv* env, jclass cls, jint format, jlong valueMicros, jstring currencyCode, jint precision){
        AdMob::systemAdPaid(static_cast<AdMobFormat>(format), static_cast<long long>(valueMicros), toString(env, currencyCode), static_cast<AdMobPrecision>(precision));
    }

    void JNICALL admobOnUserEarnedReward(JNIEnv* env, jclass cls, jint format, jstring type, jint amount){
        AdMob::systemUserEarnedReward(static_cast<AdMobFormat>(format), toString(env, type), amount);
    }

    void JNICALL admobOnAdInspectorClosed(JNIEnv* env, jclass cls, jint errorCode, jstring message){
        AdMob::systemAdInspectorClosed(errorCode, toString(env, message));
    }

    const JNINativeMethod admobNatives[] = {
        {"nativeOnInitialized", "()V", reinterpret_cast<void*>(admobOnInitialized)},
        {"nativeOnConsentUpdated", "(ILjava/lang/String;)V", reinterpret_cast<void*>(admobOnConsentUpdated)},
        {"nativeOnAdLoaded", "(IIII)V", reinterpret_cast<void*>(admobOnAdLoaded)},
        {"nativeOnAdFailedToLoad", "(IIILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdFailedToLoad)},
        {"nativeOnAdShown", "(I)V", reinterpret_cast<void*>(admobOnAdShown)},
        {"nativeOnAdFailedToShow", "(IILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdFailedToShow)},
        {"nativeOnAdDismissed", "(I)V", reinterpret_cast<void*>(admobOnAdDismissed)},
        {"nativeOnAdClicked", "(I)V", reinterpret_cast<void*>(admobOnAdClicked)},
        {"nativeOnAdImpression", "(I)V", reinterpret_cast<void*>(admobOnAdImpression)},
        {"nativeOnAdPaid", "(IJLjava/lang/String;I)V", reinterpret_cast<void*>(admobOnAdPaid)},
        {"nativeOnUserEarnedReward", "(ILjava/lang/String;I)V", reinterpret_cast<void*>(admobOnUserEarnedReward)},
        {"nativeOnAdInspectorClosed", "(ILjava/lang/String;)V", reinterpret_cast<void*>(admobOnAdInspectorClosed)},
    };

    // BillingWrapper natives

    void JNICALL billingOnInitialized(JNIEnv* env, jclass cls, jint responseCode, jstring message){
        InAppPurchase::systemInitialized(static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnDisconnected(JNIEnv* env, jclass cls){
        InAppPurchase::systemDisconnected();
    }

    void JNICALL billingOnProductsQueried(JNIEnv* env, jclass cls, jint responseCode, jstring message, jstring productsJson){
        InAppPurchase::systemProductsQueried(static_cast<BillingResponse>(responseCode), toString(env, message),
            InAppPurchase::productsFromJson(toString(env, productsJson)));
    }

    void JNICALL billingOnPurchaseUpdated(JNIEnv* env, jclass cls, jstring purchaseJson){
        for (const PurchaseDetails& purchase : InAppPurchase::purchasesFromJson(toString(env, purchaseJson))){
            InAppPurchase::systemPurchaseUpdated(purchase);
        }
    }

    void JNICALL billingOnPurchaseFailed(JNIEnv* env, jclass cls, jstring productId, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseFailed(toString(env, productId), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnPurchasesQueried(JNIEnv* env, jclass cls, jint productType, jint responseCode, jstring message, jstring purchasesJson){
        InAppPurchase::systemPurchasesQueried(static_cast<ProductType>(productType), static_cast<BillingResponse>(responseCode), toString(env, message),
            InAppPurchase::purchasesFromJson(toString(env, purchasesJson)));
    }

    void JNICALL billingOnPurchaseAcknowledged(JNIEnv* env, jclass cls, jstring purchaseToken, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseAcknowledged(toString(env, purchaseToken), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    void JNICALL billingOnPurchaseConsumed(JNIEnv* env, jclass cls, jstring purchaseToken, jint responseCode, jstring message){
        InAppPurchase::systemPurchaseConsumed(toString(env, purchaseToken), static_cast<BillingResponse>(responseCode), toString(env, message));
    }

    const JNINativeMethod billingNatives[] = {
        {"nativeOnInitialized", "(ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnInitialized)},
        {"nativeOnDisconnected", "()V", reinterpret_cast<void*>(billingOnDisconnected)},
        {"nativeOnProductsQueried", "(ILjava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnProductsQueried)},
        {"nativeOnPurchaseUpdated", "(Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseUpdated)},
        {"nativeOnPurchaseFailed", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseFailed)},
        {"nativeOnPurchasesQueried", "(IILjava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchasesQueried)},
        {"nativeOnPurchaseAcknowledged", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseAcknowledged)},
        {"nativeOnPurchaseConsumed", "(Ljava/lang/String;ILjava/lang/String;)V", reinterpret_cast<void*>(billingOnPurchaseConsumed)},
    };

    // The wrapper getter returns null when the export left the service out
    jobject findWrapper(JNIEnv* env, jobject activity, jclass activityClass, const char* getter){
        jmethodID method = env->GetMethodID(activityClass, getter, "()Ljava/lang/Object;");
        if (clearException(env) || !method)
            return nullptr;

        jobject wrapper = env->CallObjectMethod(activity, method);
        if (clearException(env))
            return nullptr;

        return wrapper;
    }

    bool registerNatives(JNIEnv* env, jclass cls, const JNINativeMethod* methods, int count){
        const bool registered = env->RegisterNatives(cls, methods, count) == JNI_OK;
        return !clearException(env) && registered;
    }

    // Stops at the first missing method: JNI must not run with an exception pending
    class MethodFinder{
    public:
        MethodFinder(JNIEnv* env, jclass cls): env(env), cls(cls) {}

        jmethodID find(const char* name, const char* signature){
            if (failed) return nullptr;
            jmethodID method = env->GetMethodID(cls, name, signature);
            if (clearException(env) || !method){
                Log::error("Java wrapper has no method %s %s", name, signature);
                failed = true;
                return nullptr;
            }
            return method;
        }

        bool ok() const { return !failed; }

    private:
        JNIEnv* env;
        jclass cls;
        bool failed = false;
    };

    // Google Mobile Ads through AdMobWrapper (java-admob)
    class AndroidAdMob: public AdMobBackend{
    public:
        jobject wrapper = nullptr; // global reference, null when the export left AdMob out

        void findMethods(MethodFinder& methods);

        virtual void initialize() override;
        virtual void setRequestConfiguration(AdMobRating rating, AdMobAgeRestriction ageRestriction, AdMobPersonalization personalization, const std::vector<std::string>& testDeviceIds) override;
        virtual void requestConsent(bool underAgeOfConsent, AdMobDebugGeography debugGeography, const std::vector<std::string>& testDeviceIds) override;
        virtual AdMobConsentStatus getConsentStatus() override;
        virtual bool canRequestAds() override;
        virtual bool isPrivacyOptionsRequired() override;
        virtual void showPrivacyOptionsForm() override;
        virtual void resetConsent() override;
        virtual void loadAd(AdMobFormat format, const std::string& adUnitId, int generation) override;
        virtual void showAd(AdMobFormat format) override;
        virtual void loadBanner(const std::string& adUnitId, AdMobBannerSize size, AdMobBannerPosition position, bool visible, int generation) override;
        virtual void setBannerVisible(bool visible) override;
        virtual void setBannerPosition(AdMobBannerPosition position) override;
        virtual void removeBanner() override;
        virtual void setServerSideVerificationOptions(const std::string& userId, const std::string& customData) override;
        virtual void setAppVolume(float volume) override;
        virtual void setAppMuted(bool muted) override;
        virtual void openAdInspector() override;

    private:
        jmethodID initializeMethod = nullptr;
        jmethodID setRequestConfigurationMethod = nullptr;
        jmethodID requestConsentMethod = nullptr;
        jmethodID getConsentStatusMethod = nullptr;
        jmethodID canRequestAdsMethod = nullptr;
        jmethodID isPrivacyOptionsRequiredMethod = nullptr;
        jmethodID showPrivacyOptionsFormMethod = nullptr;
        jmethodID resetConsentMethod = nullptr;
        jmethodID loadAdMethod = nullptr;
        jmethodID showAdMethod = nullptr;
        jmethodID loadBannerMethod = nullptr;
        jmethodID setBannerVisibleMethod = nullptr;
        jmethodID setBannerPositionMethod = nullptr;
        jmethodID removeBannerMethod = nullptr;
        jmethodID setServerSideVerificationOptionsMethod = nullptr;
        jmethodID setAppVolumeMethod = nullptr;
        jmethodID setAppMutedMethod = nullptr;
        jmethodID openAdInspectorMethod = nullptr;
    };

    // Google Play Billing through BillingWrapper (java-billing)
    class AndroidInAppPurchase: public InAppPurchaseBackend{
    public:
        jobject wrapper = nullptr; // global reference, null when the export left billing out

        void findMethods(MethodFinder& methods);

        virtual void initialize() override;
        virtual bool isReady() override;
        virtual void queryProducts(const std::vector<std::string>& productIds, ProductType type) override;
        virtual void purchase(const PurchaseParams& params) override;
        virtual void acknowledgePurchase(const std::string& purchaseToken) override;
        virtual void consumePurchase(const std::string& purchaseToken) override;
        virtual void queryPurchases(ProductType type) override;
        virtual void restorePurchases() override;
        virtual void openSubscriptionManagement(const std::string& productId) override;
        virtual void showInAppMessages() override;

    private:
        jmethodID initializeMethod = nullptr;
        jmethodID isReadyMethod = nullptr;
        jmethodID queryProductsMethod = nullptr;
        jmethodID purchaseMethod = nullptr;
        jmethodID acknowledgePurchaseMethod = nullptr;
        jmethodID consumePurchaseMethod = nullptr;
        jmethodID queryPurchasesMethod = nullptr;
        jmethodID openSubscriptionManagementMethod = nullptr;
        jmethodID showInAppMessagesMethod = nullptr;
    };

    AndroidAdMob admob;
    AndroidInAppPurchase inAppPurchase;
}

void setupServicesJNI(JNIEnv* env, jobject activity, jclass activityClass){
    if (jobject wrapper = findWrapper(env, activity, activityClass, "getAdMobWrapper")){
        jclass cls = env->GetObjectClass(wrapper);
        MethodFinder methods(env, cls);
        admob.findMethods(methods);

        if (methods.ok() && registerNatives(env, cls, admobNatives, sizeof(admobNatives) / sizeof(admobNatives[0]))){
            admob.wrapper = env->NewGlobalRef(wrapper);
        }else{
            Log::error("The AdMob Java wrapper does not match the engine, AdMob is disabled");
        }

        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(wrapper);
    }

    if (jobject wrapper = findWrapper(env, activity, activityClass, "getBillingWrapper")){
        jclass cls = env->GetObjectClass(wrapper);
        MethodFinder methods(env, cls);
        inAppPurchase.findMethods(methods);

        if (methods.ok() && registerNatives(env, cls, billingNatives, sizeof(billingNatives) / sizeof(billingNatives[0]))){
            inAppPurchase.wrapper = env->NewGlobalRef(wrapper);
        }else{
            Log::error("The Google Play Billing Java wrapper does not match the engine, in-app purchases are disabled");
        }

        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(wrapper);
    }
}

void releaseServicesJNI(JNIEnv* env){
    // the wrappers hold the activity
    if (admob.wrapper){
        env->DeleteGlobalRef(admob.wrapper);
        admob.wrapper = nullptr;
    }
    if (inAppPurchase.wrapper){
        env->DeleteGlobalRef(inAppPurchase.wrapper);
        inAppPurchase.wrapper = nullptr;
    }
}

AdMobBackend* DoriaxAndroid::getAdMobBackend(){
    return admob.wrapper ? &admob : nullptr;
}

InAppPurchaseBackend* DoriaxAndroid::getInAppPurchaseBackend(){
    return inAppPurchase.wrapper ? &inAppPurchase : nullptr;
}

void AndroidAdMob::findMethods(MethodFinder& methods){
    initializeMethod = methods.find("initialize", "()V");
    setRequestConfigurationMethod = methods.find("setRequestConfiguration", "(III[Ljava/lang/String;)V");
    requestConsentMethod = methods.find("requestConsent", "(ZI[Ljava/lang/String;)V");
    getConsentStatusMethod = methods.find("getConsentStatus", "()I");
    canRequestAdsMethod = methods.find("canRequestAds", "()Z");
    isPrivacyOptionsRequiredMethod = methods.find("isPrivacyOptionsRequired", "()Z");
    showPrivacyOptionsFormMethod = methods.find("showPrivacyOptionsForm", "()V");
    resetConsentMethod = methods.find("resetConsent", "()V");
    loadAdMethod = methods.find("loadAd", "(ILjava/lang/String;I)V");
    showAdMethod = methods.find("showAd", "(I)V");
    loadBannerMethod = methods.find("loadBanner", "(Ljava/lang/String;IIZI)V");
    setBannerVisibleMethod = methods.find("setBannerVisible", "(Z)V");
    setBannerPositionMethod = methods.find("setBannerPosition", "(I)V");
    removeBannerMethod = methods.find("removeBanner", "()V");
    setServerSideVerificationOptionsMethod = methods.find("setServerSideVerificationOptions", "(Ljava/lang/String;Ljava/lang/String;)V");
    setAppVolumeMethod = methods.find("setAppVolume", "(F)V");
    setAppMutedMethod = methods.find("setAppMuted", "(Z)V");
    openAdInspectorMethod = methods.find("openAdInspector", "()V");
}

void AndroidAdMob::initialize(){
    callVoid(wrapper, initializeMethod);
}

void AndroidAdMob::setRequestConfiguration(AdMobRating rating, AdMobAgeRestriction ageRestriction, AdMobPersonalization personalization, const std::vector<std::string>& testDeviceIds){
    JavaStringArray ids(jniEnv(), testDeviceIds);
    callVoid(wrapper, setRequestConfigurationMethod,
        static_cast<jint>(rating), static_cast<jint>(ageRestriction), static_cast<jint>(personalization), ids.get());
}

void AndroidAdMob::requestConsent(bool underAgeOfConsent, AdMobDebugGeography debugGeography, const std::vector<std::string>& testDeviceIds){
    JavaStringArray ids(jniEnv(), testDeviceIds);
    callVoid(wrapper, requestConsentMethod, static_cast<jboolean>(underAgeOfConsent), static_cast<jint>(debugGeography), ids.get());
}

AdMobConsentStatus AndroidAdMob::getConsentStatus(){
    JNIEnv* env = jniEnv();
    const jint status = env->CallIntMethod(wrapper, getConsentStatusMethod);
    if (clearException(env)) return AdMobConsentStatus::UNKNOWN;
    return static_cast<AdMobConsentStatus>(status);
}

bool AndroidAdMob::canRequestAds(){
    return callBoolean(wrapper, canRequestAdsMethod);
}

bool AndroidAdMob::isPrivacyOptionsRequired(){
    return callBoolean(wrapper, isPrivacyOptionsRequiredMethod);
}

void AndroidAdMob::showPrivacyOptionsForm(){
    callVoid(wrapper, showPrivacyOptionsFormMethod);
}

void AndroidAdMob::resetConsent(){
    callVoid(wrapper, resetConsentMethod);
}

void AndroidAdMob::loadAd(AdMobFormat format, const std::string& adUnitId, int generation){
    JavaString id(jniEnv(), adUnitId);
    callVoid(wrapper, loadAdMethod, static_cast<jint>(format), id.get(), static_cast<jint>(generation));
}

void AndroidAdMob::showAd(AdMobFormat format){
    callVoid(wrapper, showAdMethod, static_cast<jint>(format));
}

void AndroidAdMob::loadBanner(const std::string& adUnitId, AdMobBannerSize size, AdMobBannerPosition position, bool visible, int generation){
    JavaString id(jniEnv(), adUnitId);
    callVoid(wrapper, loadBannerMethod, id.get(),
        static_cast<jint>(size), static_cast<jint>(position), static_cast<jboolean>(visible), static_cast<jint>(generation));
}

void AndroidAdMob::setBannerVisible(bool visible){
    callVoid(wrapper, setBannerVisibleMethod, static_cast<jboolean>(visible));
}

void AndroidAdMob::setBannerPosition(AdMobBannerPosition position){
    callVoid(wrapper, setBannerPositionMethod, static_cast<jint>(position));
}

void AndroidAdMob::removeBanner(){
    callVoid(wrapper, removeBannerMethod);
}

void AndroidAdMob::setServerSideVerificationOptions(const std::string& userId, const std::string& customData){
    JavaString user(jniEnv(), userId);
    JavaString data(jniEnv(), customData);
    callVoid(wrapper, setServerSideVerificationOptionsMethod, user.get(), data.get());
}

void AndroidAdMob::setAppVolume(float volume){
    callVoid(wrapper, setAppVolumeMethod, static_cast<jfloat>(volume));
}

void AndroidAdMob::setAppMuted(bool muted){
    callVoid(wrapper, setAppMutedMethod, static_cast<jboolean>(muted));
}

void AndroidAdMob::openAdInspector(){
    callVoid(wrapper, openAdInspectorMethod);
}

void AndroidInAppPurchase::findMethods(MethodFinder& methods){
    initializeMethod = methods.find("initialize", "()V");
    isReadyMethod = methods.find("isReady", "()Z");
    queryProductsMethod = methods.find("queryProducts", "([Ljava/lang/String;I)V");
    purchaseMethod = methods.find("purchase", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V");
    acknowledgePurchaseMethod = methods.find("acknowledgePurchase", "(Ljava/lang/String;)V");
    consumePurchaseMethod = methods.find("consumePurchase", "(Ljava/lang/String;)V");
    queryPurchasesMethod = methods.find("queryPurchases", "(I)V");
    openSubscriptionManagementMethod = methods.find("openSubscriptionManagement", "(Ljava/lang/String;)V");
    showInAppMessagesMethod = methods.find("showInAppMessages", "()V");
}

void AndroidInAppPurchase::initialize(){
    callVoid(wrapper, initializeMethod);
}

bool AndroidInAppPurchase::isReady(){
    return callBoolean(wrapper, isReadyMethod);
}

void AndroidInAppPurchase::queryProducts(const std::vector<std::string>& productIds, ProductType type){
    JavaStringArray ids(jniEnv(), productIds);
    callVoid(wrapper, queryProductsMethod, ids.get(), static_cast<jint>(type));
}

void AndroidInAppPurchase::purchase(const PurchaseParams& params){
    JNIEnv* env = jniEnv();
    JavaString productId(env, params.productId);
    JavaString offerToken(env, params.offerToken);
    JavaString oldPurchaseToken(env, params.oldPurchaseToken);
    JavaString oldProductId(env, params.oldProductId);
    JavaString accountId(env, params.obfuscatedAccountId);
    JavaString profileId(env, params.obfuscatedProfileId);
    callVoid(wrapper, purchaseMethod, productId.get(), offerToken.get(),
        oldPurchaseToken.get(), oldProductId.get(), static_cast<jint>(params.replacementMode), accountId.get(), profileId.get());
}

void AndroidInAppPurchase::acknowledgePurchase(const std::string& purchaseToken){
    JavaString token(jniEnv(), purchaseToken);
    callVoid(wrapper, acknowledgePurchaseMethod, token.get());
}

void AndroidInAppPurchase::consumePurchase(const std::string& purchaseToken){
    JavaString token(jniEnv(), purchaseToken);
    callVoid(wrapper, consumePurchaseMethod, token.get());
}

void AndroidInAppPurchase::queryPurchases(ProductType type){
    callVoid(wrapper, queryPurchasesMethod, static_cast<jint>(type));
}

void AndroidInAppPurchase::restorePurchases(){
    // Google Play keeps the purchases of the account in sync
    queryPurchases(ProductType::INAPP);
    queryPurchases(ProductType::SUBS);
}

void AndroidInAppPurchase::openSubscriptionManagement(const std::string& productId){
    JavaString id(jniEnv(), productId);
    callVoid(wrapper, openSubscriptionManagementMethod, id.get());
}

void AndroidInAppPurchase::showInAppMessages(){
    callVoid(wrapper, showInAppMessagesMethod);
}
