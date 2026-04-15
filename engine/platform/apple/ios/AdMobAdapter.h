//
//  AdMob.h
//  Doriax iOS
//
//  Created by Eduardo Doria Lima on 11/05/23.
//

#import <Foundation/Foundation.h>

@interface AdMobAdapter : NSObject

- (nonnull instancetype)init;

- (void)initializeAdMob:(Boolean)tagForChildDirectedTreatment and:(Boolean)tagForUnderAgeOfConsent;

- (void)setMaxAdContentRating:(int)rating;

- (void)loadInterstitial:(nonnull NSString *)adUnitID;

- (bool)isInterstitialAdLoaded;

- (void)showInterstitial;

@end
