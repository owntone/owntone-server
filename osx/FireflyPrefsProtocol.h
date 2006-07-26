/*
 *  FireflyPrefsProtocol.h
 *  Firefly Helper
 *
 *  Created by Mike Kobb on 7/10/06.
 *  Copyright 2006 Roku LLC. All rights reserved.
 *
 *  This file houses the declaration of the FireflyPrefsServerProtocol
 *  and FireflyPrefsClientProtocol, which are used on the Macintosh 
 *  for communication between the Firefly prefs pane and the Firefly 
 *  Helper background app.
 *
 */

#include "FireflyCommon.h"

// The protocol for functions exported by the server (the Firefly Helper)
@protocol FireflyPrefsServerProtocol

- (BOOL)registerClient:(byref id)client withIdentifier:(int)ident;
- (oneway void)unregisterClientId:(int)ident;
- (FireflyStartResult)startFirefly;
- (FireflyStopResult)stopFirefly;
- (FireflyRestartResult)restartFirefly;
- (FireflyRescanResult)rescanLibrary;
- (FireflyServerStatus)fireflyStatus;
- (BOOL)fireflyIsRunning;
- (bycopy NSString*)fireflyVersion;
- (bycopy NSString*)fireflyConfigURL;
- (oneway void)showHelperMenu:(BOOL)bShowMenu;

@end


// The protocol for functions exported by the client (the prefs pane)
@protocol FireflyPrefsClientProtocol

- (BOOL)stillThere;
- (oneway void)statusChanged:(FireflyServerStatus)newStatus;
- (oneway void)versionChanged:(bycopy NSString*)newVersion;
- (oneway void)configUrlChanged:(bycopy NSString*)newUrl;

@end