//
//  FireflyHelper.h
//  
//  The "controller" part of our Model-View-Controller trio, plus what
//  little "view" there is for this mostly-faceless program.
//  
//  The Firefly Helper manages setup and startup of the server, as well
//  as communication withe the Prefs pane via Distributed Objects.  It
//  also optionally handles the Status Item (the menu by the clock in 
//  the menu bar).
//
//  Created by Mike Kobb on 7/12/06.
//  Copyright 2006 Roku, LLC. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "../FireflyPrefsProtocol.h"
#import "FireflyServer.h"

@interface FireflyHelper : NSObject < FireflyPrefsServerProtocol >
{
	IBOutlet NSMenu				*statusMenu;
			 NSStatusItem		*statusItem;

			 NSProtocolChecker	*protocolChecker;	
			 NSConnection		*prefsConnection;
			 
			 FireflyServer		*fireflyServer;
			 
			 // Our client
			 id					client;
			 int				clientIdent;
}

- (IBAction)startStopMenuChosen:(id)sender;
- (IBAction)prefsMenuChosen:(id)sender;

// NSApplication delegate methods
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification;
- (void)applicationWillTerminate:(NSNotification *)aNotification;

// private utilities
- (void)serverNotification:(NSNotification *)theNotification;
- (BOOL)checkClient;

// wrappers to client calls; trap exceptions
- (void)tellClientStatusChanged:(FireflyServerStatus)newStatus;
- (void)tellClientVersionChanged:(NSString *)newVersion;
- (void)tellClientURLChanged:(NSString *)newURL;

// Methods for status item
- (void)displayStatusItem;
- (void)hideStatusItem;
- (int)numberOfItemsInMenu:(NSMenu *)menu;
- (void)menuNeedsUpdate:(NSMenu *)menu;

@end
