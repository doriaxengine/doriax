//
//  AdMob.m
//  Doriax iOS
//
//  Created by Eduardo Doria Lima on 11/05/23.
//

#import "AdMobAdapter.h"

#import "AppDelegate.h"
#import "ViewController.h"
#import "Renderer.h"

#include <UserMessagingPlatform/UserMessagingPlatform.h>

@import GoogleMobileAds;

@interface AdMobAdapter () <GADFullScreenContentDelegate>

@property(nonatomic, strong) GADInterstitialAd *interstitial;

@property(nonatomic, strong) ViewController *rootController;

@end

@implementation AdMobAdapter

- (nonnull instancetype)init {
    
    _rootController = (ViewController*)[[(AppDelegate*) [[UIApplication sharedApplication]delegate] window] rootViewController];
    
    return self;
}

- (void)initializeAdMob:(Boolean)tagForChildDirectedTreatment and:(Boolean)tagForUnderAgeOfConsent {
    [GADMobileAds.sharedInstance startWithCompletionHandler:nil];
    
    [self setTagForChildDirectedTreatment: tagForChildDirectedTreatment];
    [self setTagForUnderAgeOfConsent: tagForUnderAgeOfConsent];
    
    [self requestConsent: (tagForChildDirectedTreatment || tagForUnderAgeOfConsent)];
}

- (void)setMaxAdContentRating:(int)rating {
    if (rating == 1){
        GADMobileAds.sharedInstance.requestConfiguration.maxAdContentRating = 
            GADMaxAdContentRatingGeneral;
    }else if (rating == 2){
        GADMobileAds.sharedInstance.requestConfiguration.maxAdContentRating = GADMaxAdContentRatingParentalGuidance;
    }else if (rating == 3){
        GADMobileAds.sharedInstance.requestConfiguration.maxAdContentRating =
            GADMaxAdContentRatingTeen;
    }else if (rating == 4){
        GADMobileAds.sharedInstance.requestConfiguration.maxAdContentRating =
            GADMaxAdContentRatingMatureAudience;
    }
}

- (void)setTagForChildDirectedTreatment:(Boolean)tagForChildDirectedTreatment {
    if (tagForChildDirectedTreatment){
        GADMobileAds.sharedInstance.requestConfiguration.tagForChildDirectedTreatment = @YES;
    }else{
        GADMobileAds.sharedInstance.requestConfiguration.tagForChildDirectedTreatment = @NO;
    }
}

- (void)setTagForUnderAgeOfConsent:(Boolean)tagForUnderAgeOfConsent {
    if (tagForUnderAgeOfConsent){
        GADMobileAds.sharedInstance.requestConfiguration.tagForUnderAgeOfConsent = @YES;
    }else{
        GADMobileAds.sharedInstance.requestConfiguration.tagForUnderAgeOfConsent = @NO;
    }
}

- (void)requestConsent:(Boolean)tagForUnderAgeOfConsent {
    //UMPDebugSettings *debugSettings = [[UMPDebugSettings alloc] init];
    //debugSettings.testDeviceIdentifiers = @[ GADSimulatorID ];
    //debugSettings.geography = UMPDebugGeographyEEA;
    
    // Create a UMPRequestParameters object.
    // Set tag for under age of consent. Here NO means users are not under age.
    UMPRequestParameters *parameters = [[UMPRequestParameters alloc] init];
    //parameters.debugSettings = debugSettings;
    parameters.tagForUnderAgeOfConsent = tagForUnderAgeOfConsent;
    
    //[UMPConsentInformation.sharedInstance reset];
    
    // Request an update to the consent information.
    [UMPConsentInformation.sharedInstance
     requestConsentInfoUpdateWithParameters:parameters
     completionHandler:^(NSError* _Nullable error) {
        // The consent information has updated.
        if (error) {
            // Handle the error.
            NSLog(@"Failed to request consent form with error: %@", [error localizedDescription]);
        } else {
            // The consent information state was updated.
            // You are now ready to see if a form is available.
        }
    }];
}

- (void)loadInterstitial:(NSString *)adUnitID {
    // load consent form
    UMPFormStatus formStatus = UMPConsentInformation.sharedInstance.formStatus;
    if (formStatus == UMPFormStatusAvailable) {
        [UMPConsentForm loadWithCompletionHandler:^(UMPConsentForm *form, NSError *loadError) {
            if (loadError) {
                // Handle the error.
                NSLog(@"Failed to load consent form with error: %@", [loadError localizedDescription]);
            } else {
                // Present the form. You can also hold on to the reference to present
                // later.
                if (UMPConsentInformation.sharedInstance.consentStatus == UMPConsentStatusRequired) {
                    [form
                     presentFromViewController:self->_rootController
                          completionHandler:^(NSError *_Nullable dismissError) {
                              // Handle dismissal by reloading form.
                            [self loadInterstitial:adUnitID];
                          }];
                } else {
                    // Keep the form available for changes to user consent.
                }
            }
        }];
    }
    
    // load ad
    if (UMPConsentInformation.sharedInstance.consentStatus == UMPConsentStatusObtained ||
        UMPConsentInformation.sharedInstance.consentStatus == UMPConsentStatusNotRequired){
        GADRequest *request = [GADRequest request];
        [GADInterstitialAd
         loadWithAdUnitID:adUnitID
         request:request
         completionHandler:^(GADInterstitialAd *ad, NSError *error) {
            if (error) {
                NSLog(@"Failed to load interstitial ad with error: %@", [error localizedDescription]);
                return;
            }
            self.interstitial = ad;
            self.interstitial.fullScreenContentDelegate = self;
        }];
    }
}

- (bool)isInterstitialAdLoaded {
    return (self.interstitial != nil);
}

- (void)showInterstitial {
    if (self.interstitial) {
        [self.interstitial presentFromRootViewController:_rootController];
    } else {
        NSLog(@"Ad wasn't ready");
    }
}

/// Tells the delegate that the ad failed to present full screen content.
- (void)ad:(nonnull id<GADFullScreenPresentingAd>)ad
didFailToPresentFullScreenContentWithError:(nonnull NSError *)error {
    [Renderer resumeGame];
    NSLog(@"Ad did fail to present full screen content.");
}

/// Tells the delegate that the ad will present full screen content.
- (void)adWillPresentFullScreenContent:(nonnull id<GADFullScreenPresentingAd>)ad {
    [Renderer pauseGame];
    NSLog(@"Ad will present full screen content.");
}

/// Tells the delegate that the ad dismissed full screen content.
- (void)adDidDismissFullScreenContent:(nonnull id<GADFullScreenPresentingAd>)ad {
    [Renderer resumeGame];
   NSLog(@"Ad did dismiss full screen content.");
}

@end
