package org.doriaxengine.doriax;

import android.app.Activity;
import android.content.Context;
import android.provider.Settings;
import android.util.Log;

import androidx.annotation.NonNull;

import com.google.android.gms.ads.AdError;
import com.google.android.gms.ads.AdRequest;
import com.google.android.gms.ads.FullScreenContentCallback;
import com.google.android.gms.ads.LoadAdError;
import com.google.android.gms.ads.MobileAds;
import com.google.android.gms.ads.RequestConfiguration;
import com.google.android.gms.ads.initialization.InitializationStatus;
import com.google.android.gms.ads.initialization.OnInitializationCompleteListener;
import com.google.android.gms.ads.interstitial.InterstitialAd;
import com.google.android.gms.ads.interstitial.InterstitialAdLoadCallback;
import com.google.android.ump.ConsentDebugSettings;
import com.google.android.ump.ConsentForm;
import com.google.android.ump.ConsentInformation;
import com.google.android.ump.ConsentRequestParameters;
import com.google.android.ump.FormError;
import com.google.android.ump.UserMessagingPlatform;

public class AdMobWrapper {
    private Activity activity;

    private InterstitialAd interstitialAd;

    private ConsentInformation consentInformation;
    private ConsentForm consentForm;

    public AdMobWrapper(final Activity activity) {
        this.activity = activity;
    }

    public void initialize(boolean tagForChildDirectedTreatment, boolean tagForUnderAgeOfConsent){
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                // Initialize the Mobile Ads SDK.
                MobileAds.initialize(activity, new OnInitializationCompleteListener() {
                    @Override
                    public void onInitializationComplete(InitializationStatus initializationStatus) {}
                });

                setTagForChildDirectedTreatment(tagForChildDirectedTreatment);
                setTagForUnderAgeOfConsent(tagForUnderAgeOfConsent);

                requestConsent(tagForChildDirectedTreatment || tagForUnderAgeOfConsent);
            }
        });
    }

    private void setMaxAdContentRating(int rating){
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                String tagValue;
                if (rating == 1) {
                    tagValue = RequestConfiguration.MAX_AD_CONTENT_RATING_G;
                }else if (rating == 2){
                    tagValue = RequestConfiguration.MAX_AD_CONTENT_RATING_PG;
                }else if (rating == 3){
                    tagValue = RequestConfiguration.MAX_AD_CONTENT_RATING_T;
                }else if (rating == 4){
                    tagValue = RequestConfiguration.MAX_AD_CONTENT_RATING_MA;
                }else{
                    tagValue = RequestConfiguration.MAX_AD_CONTENT_RATING_UNSPECIFIED;
                }

                RequestConfiguration requestConfiguration = MobileAds.getRequestConfiguration()
                        .toBuilder()
                        .setMaxAdContentRating(tagValue)
                        .build();
                MobileAds.setRequestConfiguration(requestConfiguration);
            }
        });
    }

    private void setTagForChildDirectedTreatment(boolean enable){
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                int tagValue;
                if (enable) {
                    tagValue = RequestConfiguration.TAG_FOR_CHILD_DIRECTED_TREATMENT_TRUE;
                }else{
                    tagValue = RequestConfiguration.TAG_FOR_CHILD_DIRECTED_TREATMENT_FALSE;
                }

                RequestConfiguration requestConfiguration = MobileAds.getRequestConfiguration()
                        .toBuilder()
                        .setTagForChildDirectedTreatment(tagValue)
                        .build();
                MobileAds.setRequestConfiguration(requestConfiguration);
            }
        });
    }

    private void setTagForUnderAgeOfConsent(boolean enable){
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                int tagValue;
                if (enable) {
                    tagValue = RequestConfiguration.TAG_FOR_UNDER_AGE_OF_CONSENT_TRUE;
                }else{
                    tagValue = RequestConfiguration.TAG_FOR_UNDER_AGE_OF_CONSENT_FALSE;
                }

                RequestConfiguration requestConfiguration = MobileAds.getRequestConfiguration()
                        .toBuilder()
                        .setTagForUnderAgeOfConsent(tagValue)
                        .build();
                MobileAds.setRequestConfiguration(requestConfiguration);
            }
        });
    }

    private void requestConsent(boolean tagForUnderAgeOfConsent){
        //ConsentDebugSettings debugSettings = new ConsentDebugSettings.Builder(activity)
        //        .setDebugGeography(ConsentDebugSettings.DebugGeography.DEBUG_GEOGRAPHY_EEA)
        //        .addTestDeviceHashedId("TEST-DEVICE-HASHED-ID")
        //        .build();

        // Set tag for under age of consent. false means users are not under
        // age.
        ConsentRequestParameters params = new ConsentRequestParameters
                .Builder()
                // call requestConsentInfoUpdate() without setting this value, your app logs the required ID hash when run
                //.setConsentDebugSettings(debugSettings)
                .setTagForUnderAgeOfConsent(tagForUnderAgeOfConsent)
                .build();

        consentInformation = UserMessagingPlatform.getConsentInformation(activity);
        //consentInformation.reset();

        consentInformation.requestConsentInfoUpdate(
                activity,
                params,
                new ConsentInformation.OnConsentInfoUpdateSuccessListener() {
                    @Override
                    public void onConsentInfoUpdateSuccess() {
                        // The consent information state was updated.
                        // You are now ready to check if a form is available.
                    }
                },
                new ConsentInformation.OnConsentInfoUpdateFailureListener() {
                    @Override
                    public void onConsentInfoUpdateFailure(FormError formError) {
                        // Handle the error.
                    }
                });
    }

    public void loadInterstitialAd(String adUnitID) {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                // load consent form
                if (consentInformation.isConsentFormAvailable()) {
                    UserMessagingPlatform.loadConsentForm(
                            activity,
                            new UserMessagingPlatform.OnConsentFormLoadSuccessListener() {
                                @Override
                                public void onConsentFormLoadSuccess(ConsentForm consentForm) {
                                    AdMobWrapper.this.consentForm = consentForm;
                                    if (consentInformation.getConsentStatus() == ConsentInformation.ConsentStatus.REQUIRED) {
                                        consentForm.show(
                                                activity,
                                                new ConsentForm.OnConsentFormDismissedListener() {
                                                    @Override
                                                    public void onConsentFormDismissed(FormError formError) {
                                                        // Handle dismissal by reloading form.
                                                        loadInterstitialAd(adUnitID);
                                                    }
                                                });
                                    }
                                }
                            },
                            new UserMessagingPlatform.OnConsentFormLoadFailureListener() {
                                @Override
                                public void onConsentFormLoadFailure(FormError formError) {
                                    // Handle Error.
                                }
                            }
                    );
                }

                // load ad
                if (consentInformation.getConsentStatus() == ConsentInformation.ConsentStatus.OBTAINED ||
                        consentInformation.getConsentStatus() == ConsentInformation.ConsentStatus.NOT_REQUIRED) {
                    AdRequest adRequest = new AdRequest.Builder().build();
                    InterstitialAd.load(
                            activity,
                            adUnitID,
                            adRequest,
                            new InterstitialAdLoadCallback() {
                                @Override
                                public void onAdLoaded(@NonNull InterstitialAd interstitialAd) {
                                    // The mInterstitialAd reference will be null until
                                    // an ad is loaded.
                                    AdMobWrapper.this.interstitialAd = interstitialAd;

                                    interstitialAd.setFullScreenContentCallback(
                                            new FullScreenContentCallback() {
                                                @Override
                                                public void onAdDismissedFullScreenContent() {
                                                    // Called when fullscreen content is dismissed.
                                                    // Make sure to set your reference to null so you don't
                                                    // show it a second time.
                                                    AdMobWrapper.this.interstitialAd = null;
                                                }

                                                @Override
                                                public void onAdFailedToShowFullScreenContent(AdError adError) {
                                                    // Called when fullscreen content failed to show.
                                                    // Make sure to set your reference to null so you don't
                                                    // show it a second time.
                                                    AdMobWrapper.this.interstitialAd = null;
                                                    Log.e("Doriax", "The ad failed to show.");
                                                }

                                                @Override
                                                public void onAdShowedFullScreenContent() {
                                                    // Called when fullscreen content is shown.
                                                }
                                            });
                                }

                                @Override
                                public void onAdFailedToLoad(@NonNull LoadAdError loadAdError) {
                                    // Handle the error
                                    Log.e("Doriax", loadAdError.getMessage());
                                    interstitialAd = null;
                                }
                            });
                }
            }
        });

    }

    public boolean isInterstitialAdLoaded(){
        return interstitialAd != null;
    }

    public void showInterstitialAd() {
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                // avoid show ads when Firebase Test Lab
                String testLabSetting = Settings.System.getString(activity.getContentResolver(), "firebase.test.lab");
                if (!"true".equals(testLabSetting)) {

                    if (isInterstitialAdLoaded()) {
                        interstitialAd.show(activity);
                    }

                }
            }
        });
    }
}
