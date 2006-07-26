//
//  FireflyHelper.m
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

#import "FireflyHelper.h"
#import "FireflyServer.h"
#include "../FireflyCommon.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sysctl.h>

@implementation FireflyHelper

// ---------------------------------------------------------------------------
// init
//
// Note that we create this path in this way primarily because we cribbed
// the code from the prefs pane, and we eventually will probably 
// change both, so for now it's best to be consistent.
// ---------------------------------------------------------------------------
- (id)init
{
	protocolChecker = nil;
	prefsConnection = nil;
	fireflyServer = nil;
	statusItem = nil;
	client = nil;
	clientIdent = 0;
	
	return self;
}

// ---------------------------------------------------------------------------
// dealloc
// ---------------------------------------------------------------------------
- (void)dealloc
{
	[protocolChecker release];
	[prefsConnection release];
	[statusItem release];
	[FireflyServer release];
	
	[super dealloc];
}

// ---------------------------------------------------------------------------
// applicationDidFinishLaunching
//
// We implement this delegate method so that we will be called when the
// Firefly Helper app is launched.  We check to see if the prefs say
// we should launch the server when we launch, and if they do, we launch.
// ---------------------------------------------------------------------------
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Perform any checking of the installation here.  Probably just look for
	// the config file and put up a dialog if not found.
	
	bool bSuccess = false;
	NSString *errorString = @"";
	do // while(false)
	{
		// Create and initialize our fireflyServer object
		NSString *serverPath = 
		[[NSBundle bundleForClass:[self class]] pathForResource:@"firefly" 
														 ofType:nil 
													inDirectory:@"Server"];
		fireflyServer = 
			[[[FireflyServer alloc] initWithServerPath:serverPath] retain];
		if( nil == fireflyServer )
		{
			errorString = 
				NSLocalizedString( @"Failed to initialize Firefly server",
								   @"explanatory text for failure to launch Firefly helper" );
			break;
		}
		
		// Register for notifications from our server.  Go ahead and do this before we
		// start it.
		[[NSNotificationCenter defaultCenter] addObserver:self
												 selector:@selector(serverNotification:)
													 name:@STATUS_CHANGE
												   object:fireflyServer ];
		[[NSNotificationCenter defaultCenter] addObserver:self
												 selector:@selector(serverNotification:)
													 name:@VERSION_CHANGE
												   object:fireflyServer ];
		[[NSNotificationCenter defaultCenter] addObserver:self
												 selector:@selector(serverNotification:)
													 name:@URL_CHANGE
												   object:fireflyServer ];
		
		// Must call this or Bonjour stuff won't work
		[fireflyServer setup];
		
		CFBooleanRef shouldLaunch = CFPreferencesCopyAppValue( CFSTR(FF_PREFS_LAUNCH_AT_LOGIN),
															   CFSTR(FF_PREFS_DOMAIN) );
		if( NULL != shouldLaunch )
		{
			if( CFBooleanGetValue( shouldLaunch ) )
				[fireflyServer start];
			CFRelease( shouldLaunch );
		}
		
		// Okay, we're open for business.  Let's vend our interface for
		// the prefs pane to use
		CFStringRef userName = CSCopyUserName( false );
		NSString *serviceName = [@"FireflyHelper" stringByAppendingString:(NSString*)userName];
		CFRelease(userName);
		protocolChecker = [NSProtocolChecker 
			protocolCheckerWithTarget:self 
							 protocol:@protocol(FireflyPrefsServerProtocol)];
		prefsConnection = [NSConnection defaultConnection];
		[prefsConnection setRootObject:protocolChecker];
		if( ![prefsConnection registerName:serviceName] )
		{
			errorString = 
				NSLocalizedString( @"Unable to open communication channel for Preference pane",
								   @"explanatory text for failure to luanch Firefly helper" );
			break;
		}	
		
		// Made it through!
		bSuccess = true;
		
		// If we're supposed to put up a Menu Extra (NSStatusItem), do so
		CFBooleanRef showMenu = CFPreferencesCopyAppValue( CFSTR(FF_PREFS_SHOW_MENU_EXTRA),
														   CFSTR(FF_PREFS_DOMAIN) );
		if( NULL != showMenu )
		{
			if( CFBooleanGetValue( showMenu ) )
				[self displayStatusItem];
			CFRelease( showMenu );
		}
		
	} while( false );
	
	// If we encountered a critical failure, we need to display an alert and
	// then quit.
	if( !bSuccess )
	{
		NSString *alertString = NSLocalizedString( @"Firefly cannot start",
												   @"Alert message when firefly helper can't start" );
		NSString *quitString = NSLocalizedString( @"Quit",
												  @"Label for quit button in failure alert" );
		
		NSAlert *alert = [NSAlert alertWithMessageText:alertString
										 defaultButton:quitString
									   alternateButton:nil
										   otherButton:nil
							 informativeTextWithFormat:errorString];
		
		[alert setAlertStyle:NSCriticalAlertStyle];
		[NSApp activateIgnoringOtherApps:YES];
		[alert runModal];
		[NSApp terminate:self];
	}
}

// ---------------------------------------------------------------------------
// applicationWillTerminate
//
// We implement this delegate method so that we will be called when the
// Firefly Helper app is quitting.  When the user logs out, we quit their
// Firefly server process.
// ---------------------------------------------------------------------------
- (void)applicationWillTerminate:(NSNotification *)aNotification
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	// Notify the prefs pane we're bailing?
	[fireflyServer shutdown];  // also stops the server
	[prefsConnection invalidate];
}

// ---------------------------------------------------------------------------
// serverNotification
//
// Our handler for notifications from the server
// ---------------------------------------------------------------------------
- (void)serverNotification:(NSNotification *)theNotification
{
	if( [[theNotification name] isEqualToString:@STATUS_CHANGE] )
	{
		[self tellClientStatusChanged:[fireflyServer status]];
	}
	else if( [[theNotification name] isEqualToString:@VERSION_CHANGE] )
	{
		[self tellClientVersionChanged:[fireflyServer version]];
	}
	else if( [[theNotification name] isEqualToString:@URL_CHANGE] )
	{
		[self tellClientURLChanged:[fireflyServer configURL]];
	}
}

// ---------------------------------------------------------------------------
// checkClient
//
// Call this function to see if a client is really connected.  If our client
// ivar is nil, returns NO, but if it's not nil, we try to "ping" the client.
// If that ping fails, the client ivar is released and set to nil, and we
// also return NO.
// ---------------------------------------------------------------------------
- (BOOL)checkClient
{
	BOOL bRetVal = NO;
	@try
	{
		bRetVal = [client stillThere];
	}
	@catch( NSException *exception )
	{
		[client autorelease];
		client = nil;
		NSLog( @"Client disappeared; setting to nil" );
	}
	
	return bRetVal;
}

// ---------------------------------------------------------------------------
// tellClientStatusChanged
//
// A wrapper for the call to statusChanged, that tests client for nil and
// traps any IPC-related exception.  If an exception fires, this function
// will call our checkClient function to see if the client is still valid.
// ---------------------------------------------------------------------------
- (void)tellClientStatusChanged:(FireflyServerStatus)newStatus
{
	if( nil != client )
	{
		@try
		{
			[client statusChanged:newStatus];
		}
		@catch( NSException *exception )
		{
			[self checkClient];
		}
	}
}

// ---------------------------------------------------------------------------
// tellClientVersionChanged
//
// A wrapper for the call to versionChanged.  See tellClientStatusChanged
// for details.
// ---------------------------------------------------------------------------
- (void)tellClientVersionChanged:(NSString *)newVersion
{
	if( nil != client )
	{
		@try
		{
			[client versionChanged:newVersion];
		}
		@catch( NSException *exception )
		{
			[self checkClient];
		}
	}
}

// ---------------------------------------------------------------------------
// tellClientURLChanged
//
// A wrapper for the call to configUrlChanged.  See tellClientStatusChanged
// for details.
// ---------------------------------------------------------------------------
- (void)tellClientURLChanged:(NSString *)newURL
{
	if( nil != client )
	{
		@try
		{
			[client configUrlChanged:newURL];
		}
		@catch( NSException * )
		{
			[self checkClient];
		}
	}
}

// ===========================================================================
// Items pertaining to the Status Item (our little menu bar item)
// ===========================================================================

// ---------------------------------------------------------------------------
// displayStatusItem
//
// Adds our Firefly menu to the menu bar
// ---------------------------------------------------------------------------
- (void)displayStatusItem
{
	if( nil == statusItem )
	{
		statusItem = [[[NSStatusBar systemStatusBar] statusItemWithLength:
			NSSquareStatusItemLength] retain];
		if( nil != statusItem )
		{
			[statusMenu setDelegate:self];
			[statusItem setMenu:statusMenu];
			NSImage *statusImage = 
				[[NSImage alloc] initWithContentsOfFile:
					[[NSBundle mainBundle] pathForResource:@"ff_logo_status_menu" 
													ofType:@"tif"]];
			[statusItem setImage:statusImage];
			[statusItem setHighlightMode:YES];
		}
	}
}

// ---------------------------------------------------------------------------
// hideStatusItem
//
// Takes our Firefly menu out of the menu bar
// ---------------------------------------------------------------------------
- (void)hideStatusItem
{
	if( nil != statusItem )
	{
		[[statusItem statusBar] removeStatusItem:statusItem];
		[statusItem autorelease];
		statusItem = nil;
	}
}

// ---------------------------------------------------------------------------
// startStopMenuChosen
//
// Somebody chose "Start Firefly" or "Stop Firefly" from the menu
// ---------------------------------------------------------------------------
- (IBAction)startStopMenuChosen:(id)sender
{
	if( nil != fireflyServer )
	{
		if( [fireflyServer isRunning] )
			[fireflyServer stop];
		else
			[fireflyServer start];
	}
}

// ---------------------------------------------------------------------------
// prefsMenuChosen
//
// Somebody chose "Firefly Preferences…"
// ---------------------------------------------------------------------------
- (IBAction)prefsMenuChosen:(id)sender
{
	NSDictionary *errorDict = nil;
	NSString *scriptSource = @"tell application \"System Preferences\"\n"
		"activate\n"
		"set current pane to pane \"org.fireflymediaserver.prefpanel\"\n"
		"end tell\n";
	NSAppleScript *myScript = [[NSAppleScript alloc] initWithSource:scriptSource];
	
	if( nil != myScript )
	{
		[myScript executeAndReturnError:&errorDict];
		[myScript release];
	}
}

// ---------------------------------------------------------------------------
// numberOfItemsInMenu
//
// NSMenu delegate method.  We always return -1 because we don't change the
// number of items in the menu.
// ---------------------------------------------------------------------------
- (int)numberOfItemsInMenu:(NSMenu *)menu
{
	return -1;
}

// ---------------------------------------------------------------------------
// menuNeedsUpdate
//
// NSMenu delegate method.  If our status has changed, we update the menu
// item text.
// ---------------------------------------------------------------------------
- (void)menuNeedsUpdate:(NSMenu *)menu
{
	if( menu == statusMenu && 3 == [menu numberOfItems] )
	{
		// Just need to update the status of the server and the start/stop
		// menu
		id item = [menu itemAtIndex:0];
		[item setTitle:StringForFireflyStatus( [self fireflyStatus] )];
		
		item = [menu itemAtIndex:1];
		if( [self fireflyIsRunning] )
		{
			[item setTitle:NSLocalizedString( @"Stop Firefly",
											  @"Text for status menu" )];
		}
		else
		{
			[item setTitle:NSLocalizedString( @"Start Firefly",
											  @"Text for status menu" )];
		}
	}
}

// ===========================================================================
// Implementation of FireflyPrefsProtocol
// ===========================================================================

// ---------------------------------------------------------------------------
// registerClient
//
// When the Prefs pane starts up and connects to us, it will register itself
// with us so that we may notify it of changes in the server status while
// it's open.  This registration process also gives us the ability to detect
// when a 
// ---------------------------------------------------------------------------
- (BOOL)registerClient:(id) newClient withIdentifier:(int) ident
{
	BOOL bRetVal = NO;
	if( nil != client )
	{
		// Hm.  We already have a client connected.  Let's see if it's really
		// still there.  This will set client to nil if the client has died.
		if( [self checkClient] )
			NSLog(@"registerClient called, but valid client already connected!\n");
	}
	
	if( nil == client )
	{
		client = [newClient retain];
		clientIdent = ident;
		bRetVal = YES;
	}
	
	return bRetVal;
}

// ---------------------------------------------------------------------------
// unregisterClientId
//
// When the Prefs pane is closing, it unregisters, so we know not to try to
// send it more udpates
// ---------------------------------------------------------------------------
- (void)unregisterClientId:(int) ident
{
	if( ident == clientIdent )
	{
		clientIdent = 0;
		[client autorelease];
		client = nil;
	}
}

// ---------------------------------------------------------------------------
// startFirefly
//
// Starts the server.  Return value indicates that the server process was
// started.  If a client has registered for status updates, it will see the
// status kFireflyStatusStarting.  The server could possibly quit before
// coming online, so clients really should register for status updates.
// ---------------------------------------------------------------------------
- (FireflyStartResult)startFirefly
{
	FireflyStartResult retVal = kFireflyStartFail;
	if( nil != fireflyServer && [fireflyServer start] )
		retVal = kFireflyStartSuccess;
	return retVal;
}

// ---------------------------------------------------------------------------
// stopFirefly
//
// Signals the server to stop.  A successful return value indicates that the
// server was not running, or has been successfully signaled to stop.  
// Clients should register for status updates and look for the actual change
// to kFireflyStatusStopped to confirm shutdown.
// ---------------------------------------------------------------------------
- (FireflyStopResult)stopFirefly
{
	FireflyStopResult retVal = kFireflyStopFail;
	if( nil != fireflyServer && [fireflyServer stop] )
		retVal = kFireflyStopSuccess;
	return retVal;
}

// ---------------------------------------------------------------------------
// rescanLibrary
//
// Tells the server to re-scan the library.  Returns a failure result if
// the server is not running.
// ---------------------------------------------------------------------------
- (FireflyRescanResult)rescanLibrary
{
	FireflyRescanResult retVal = kFireflyRescanInvalid;
	if( nil != fireflyServer )
		retVal = [fireflyServer status];
	return retVal;
}

// ---------------------------------------------------------------------------
// fireflyStatus
//
// Replies with the state of the server
// ---------------------------------------------------------------------------
- (FireflyServerStatus)fireflyStatus
{
	FireflyServerStatus retVal = kFireflyStatusInvalid;
	if( nil != fireflyServer )
		retVal = [fireflyServer status];
	return retVal;
}

// ---------------------------------------------------------------------------
// stopFirefly
//
// Signals the server to restart.  A successful return value indicates that 
// the server has been successfully signaled to restart.
// Clients should register for status updates and look for the actual changes
// in server status to verify that the signal is handled.
// ---------------------------------------------------------------------------
- (FireflyRestartResult)restartFirefly
{
	FireflyRestartResult retVal = kFireflyRestartFail;
	if( nil != fireflyServer && [fireflyServer restart] )
		retVal = kFireflyRestartSuccess;
	return retVal;
}

// ---------------------------------------------------------------------------
// fireflyVersion
//
// Replies with the version of the server.  Returns nil if the
// server is not running or we are unable to ascertain the version.  Note
// that a method invocation on nil returns nil, so we don't need to check 
// fireflyServer for nil.
// ---------------------------------------------------------------------------
- (NSString *)fireflyVersion
{
	return [fireflyServer version];
}

// ---------------------------------------------------------------------------
// fireflyConfigURL
//
// Replies with the URL to the advanced configuration page for the server.  
// Returns nil if the server is not running or we are unable to 
// ascertain the URL.  See note about nil above.
// ---------------------------------------------------------------------------
- (NSString *)fireflyConfigURL
{
	return [fireflyServer configURL];
}

// ---------------------------------------------------------------------------
// showHelperMenu
//
// Allows the prefs pane to specify whether to show the item.  This setting
// is persistent
// ---------------------------------------------------------------------------
- (void)showHelperMenu:(BOOL)bShowMenu
{
	if( bShowMenu )
		[self displayStatusItem];
	else
		[self hideStatusItem];
}

// ---------------------------------------------------------------------------
// fireflyVersion
//
// Returns YES if the server is running, NO if not
// ---------------------------------------------------------------------------
- (BOOL)fireflyIsRunning
{
	BOOL retVal = NO;
	if( nil != fireflyServer )
		retVal = [fireflyServer isRunning];
	
	return retVal;
}

// ===========================================================================
// Implementation of NSMenuValidation
// ===========================================================================

// ---------------------------------------------------------------------------
// validateMenuItem
//
// Our first item is always disabled.  Our last is always enabled.  The
// one in the middle depends upon our status
// ---------------------------------------------------------------------------
- (BOOL)validateMenuItem:(id <NSMenuItem>)menuItem
{
	BOOL bRetVal = NO;
	if( nil != statusMenu && 3 == [statusMenu numberOfItems] )
	{
		if( menuItem == [statusMenu itemAtIndex:2] )
			bRetVal = YES;
		else if( menuItem == [statusMenu itemAtIndex:1] )
		{
			FireflyServerStatus status = [self fireflyStatus];
			if( status != kFireflyStatusStopping && status != kFireflyStatusInvalid )
				bRetVal = YES;
		}
	}
	return bRetVal;
}


@end
