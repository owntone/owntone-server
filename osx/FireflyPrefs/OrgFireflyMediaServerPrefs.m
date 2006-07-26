// NOTE: The canonical Cocoa way to do things is with a strict Model-View-Controller
// organization.  However, that seems a bit silly for a simple case like a prefs
// pane, so the OrgFireflyMediaServerPrefs object is both model and controller.

#import <Foundation/NSPathUtilities.h>
#import <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#import "OrgFireflyMediaServerPrefs.h"
#include "../FireflyCommon.h"

// Here we define some constants used when testing the existence of and accessing
// the components of our installation.
#define FIREFLY_HELPER_NAME		"Firefly Helper.app"
#define FIREFLY_HELPER_PROC_N	"Firefly Helper"
#define FIREFLY_PLUGIN_DIR		"plugins"
#define FIREFLY_LOG_FILE		"firefly.log"
#define FIREFLY_PLAYLIST_FILE	"firefly.playlist"

@implementation OrgFireflyMediaServerPrefs

// ===========================================================================
// Initialization and deallocation
// ===========================================================================
- (id)initWithBundle:(NSBundle *)bundle
{
	if( ( self = [super initWithBundle:bundle] ) != nil )
	{
		appID = CFSTR( "org.fireflymediaserver.prefs" );
		
		// Init our instance variables
		configFileStrings = [[NSMutableArray arrayWithCapacity:100] retain];
		configError = [[NSMutableString stringWithCapacity:20] retain];
		fireflyFolderPath = [[NSMutableString stringWithCapacity:20] retain];
		fireflyHelperPath = [[NSMutableString stringWithCapacity:20] retain];
		serverURL = [[NSMutableString stringWithCapacity:20] retain];
		logFilePath = [[NSMutableString stringWithCapacity:20] retain];
		playlistPath = [[NSMutableString stringWithCapacity:20] retain];
		userName = nil;
		configFilePath = [[NSMutableString stringWithCapacity:20] retain];
		serverName = [[NSMutableString stringWithCapacity:20] retain];
		serverPassword = [[NSMutableString stringWithCapacity:20] retain];
		libraryPath = [[NSMutableString stringWithCapacity:20] retain];
		serverProxy = nil;
		protocolChecker = nil;
		ipcTimer = nil;
		logTimer = nil;
		logDate = nil;
		srand((unsigned int)time(NULL));
	}
	
	return self;
}

- (void)dealloc
{
	[configFileStrings release];
	[configError release];
	[fireflyFolderPath release];
	[fireflyHelperPath release];
	[serverURL release];
	[logFilePath release];
	[playlistPath release];
	[userName release];
	[configFilePath release];
	[serverName release];
	[serverPassword release];
	[libraryPath release];
	[serverProxy release];
	[protocolChecker release];
	[ipcTimer release];
	[logTimer release];
	[logDate release];
	[super dealloc];
}

// ===========================================================================
// NSPreferencePane methods for handling the installation and removal of
// the panel.  We use these to read our prefs, set up our UI, and start
// and stop our scan for the server, as well as confirming whether a user
// wants to apply changes.
// ===========================================================================

// ---------------------------------------------------------------------------
// willSelect
//
// NSPreferencePane instance method.  We're about to be put on screen.
// ---------------------------------------------------------------------------
- (void)willSelect
{
	// NOTE: docs say default impl does nothing, so not necessary to call [super willSelect];
	
	// Set up our user name (used for the library name as as IPC).  Must do
	// this early, because setDefaultValues will need it to make the library
	// name.  ("Copy" function name means no need to retain but we do need
	// to release later.  CSStringRef is toll-free bridged to NSString*)
	[userName autorelease]; // in case we are being re-loaded within one Prefs session
	userName = (NSString*)CSCopyUserName( false );
	
	// We're about to be loaded.  Set up everything
	[self setDefaultValues];
	
	// This is a bit of a hack.  bConfigNeedsSaving will be set to YES upon
	// exit from validateInstall if validateInstall had to create a new
	// prefs file.  We use this as a cue that it's a fresh install, and we
	// need to get the startup item installed (by calling saveSettings)
	bConfigNeedsSaving = NO;
	
	if( ![self validateInstall] )
	{
		[self disableAllControls];
		bConfigNeedsSaving = NO;
	}
	else
	{
		if(![self loadSettings])
		{
			[configError setString:NSLocalizedString( @"Unable to read configuration information",
													  @"Error message related to invalid config" ) ];
			configAppearsValid = NO;
		}
		else
		{
			// If ValidateInstall told us it created a new file, then we are
			// going to do some hacky things.  First, set bStartServerOnLogin to
			// false and save settings.  This ensures that when we start firefly
			// Helper in a few seconds, it does not launch the server before the
			// user has a chance to set their settings.  Then, we'll set it
			// back to its original value, and leave bConfigNeedsSaving
			// set.  This way, when the user closes the panel or starts the server,
			// their changes will be set.  Ugh.
			if( bConfigNeedsSaving )
			{
				BOOL priorVal = bStartServerOnLogin;
				bStartServerOnLogin = NO;
				[self saveSettings];
				CFPreferencesAppSynchronize( CFSTR(FF_PREFS_DOMAIN) ); // flush changes
				bConfigNeedsSaving = YES; // saveSettings sets to NO
				bStartServerOnLogin = priorVal;
			}
		}
	}
	
	// Snag our current version
	NSString *versionString = [[NSBundle bundleForClass:[self class]] 
		objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	[panelVersionText setStringValue:versionString];
	
	
	if( configAppearsValid )
	{
		// GUI setup to initial state (note that although some of these are set in
		// the nib, we may be closed and then re-opened, so we need to set them
		// here.
		[browseButton setEnabled:YES];
		[nameField setEnabled:YES];
		[passwordCheckbox setEnabled:YES];
		[helperMenuCheckbox setEnabled:YES];
		[serverStartOptions setEnabled:YES];
		[mainTabView selectFirstTabViewItem:self];
		[nameField setStringValue:serverName];
		[libraryField setStringValue:libraryPath];
		[self setIconForPath];
		[passwordField setStringValue:serverPassword];
		if( [serverPassword length] > 0 )
		{
			[passwordCheckbox setState:NSOnState];
			[passwordField setEnabled:YES];
		}
		else
		{
			[passwordCheckbox setState:NSOffState];
			[passwordField setEnabled:NO];
		}
		[portField setIntValue:serverPort];
		if( 0 != serverPort )
		{
			[portField setEnabled:YES];
			[portPopup selectItemAtIndex:1];
		}
		else
		{
			[portField setEnabled:NO];
			[portPopup selectItemAtIndex:0];
		}
		if( bStartServerOnLogin )
			[serverStartOptions selectItemAtIndex:1];
		else
			[serverStartOptions selectItemAtIndex:0];

		// bConfigNeedsSaving is configured above
		[applyNowButton setEnabled:bConfigNeedsSaving];

		[helperMenuCheckbox setState:(bShowHelperMenu ? NSOnState : NSOffState)];

		// Member setup to initial state (note, these are not our actual
		// preferences, which are set above).  Rather, these are members for running
		// the prefs pane.
		serverProxy = nil;
		ipcTimer = nil;
		logTimer = nil;
		[logDate autorelease];
		logDate = [[NSDate distantPast] retain];
		
		// Start by assuming that the server is not running.
		[self updateServerStatus:kFireflyStatusStopped];
		
		// We always need the helper running when the panel is running,
		// so launch it if it's not already running
		[self launchHelperIfNeeded];
	}
}

// ---------------------------------------------------------------------------
// didSelect
//
// NSPreferencePane instance method.  We're now on screen.
// ---------------------------------------------------------------------------
- (void)didSelect
{
	// NOTE: docs say default impl does nothing, so not necessary to call [super didSelect];
	
	// We've been loaded and are on screen.

	// Did we encounter any errors at startup that will prevent us from doing work?  If so,
	// here's where we put up a sheet to explain.
	if( configAppearsValid )
	{
		// No errors.  We could go ahead and try right now to establish
		// Connection.  BUT, since we may be being opened in response to the
		// Helper application's menu choice, we avoid a possible (temporary)
		// deadlock by doing our first proxy attempt in the timer function,
		// which allows didSelect to return and let the Apple Event complete.
#if 0
		// 
		if( [self makeProxyConnection] )
		{
			[self updateServerStatus:[self fireflyStatus]];
			NSString *string = [self fireflyVersion];
			if( nil != string )
				[self versionChanged:string];
			string = [self fireflyConfigURL];
			if( nil != string )
				[self configUrlChanged:string];
		}
		else
#endif
		{
			[startStopButton setEnabled:NO];
			[statusText setStringValue:NSLocalizedString( @"Checking Firefly status…", 
														  @"Status text for when Firefly state is not known" )];
			[progressSpinner startAnimation:self];
			ipcTimer = [[NSTimer scheduledTimerWithTimeInterval:1.0
														 target:self
													   selector:@selector(proxyTimerFired:)
													   userInfo:nil
														repeats:YES] retain];
		}
	}
	else
	{
		NSString *errorIntro = NSLocalizedString( @"Firefly appears to be incorrectly installed or damaged. "
												  "Please consult the documentation.\n\n",
												  @"Explanatory text for the failure-to-apply alert" );
		NSString *errorString = [errorIntro stringByAppendingString:configError];
		NSBeginCriticalAlertSheet( NSLocalizedString( @"Configuration error",
													  @"Alert message notifying the user of config error" ),
								   @"OK", 
								   NULL, 
								   NULL, 
								   [[self mainView] window], 
								   nil, 
								   NULL, 
								   NULL, 
								   NULL, 
								   errorString );
	}
}

// ---------------------------------------------------------------------------
// shouldUnselect
//
// NSPreferencePane delegate method
// ---------------------------------------------------------------------------
- (NSPreferencePaneUnselectReply)shouldUnselect
{
	// We write our config when the user clicks "Apply Now".  If they've made changes
	// but not clicked the button, we need to ask them here if they want to save.
	
	// if changes need saving, we want to put up a sheet asking if they want to apply
	// NOTE: Sheets are complicated to deal with, because you have to handle their results
	// in delegate methods, and it gets a bit wonky if handling the result in turn
	// requires another modal dialog.  Anyway, we post the sheet here.  Look for sheetDidEnd to
	// see the handling of the result.
	if( bConfigNeedsSaving )
	{
		// Even more complicated than the average sheet (where we could call NSBeginAlertSheet),
		// because we offer Cmd-D for "Don't apply"
		NSAlert *alert = [[NSAlert alloc] init];
		[alert setMessageText:NSLocalizedString( @"Apply configuration changes?",
												 @"Prompt to save changes when exiting prefs pane" )];
		[alert addButtonWithTitle:NSLocalizedString( @"Apply", @"Label for apply button in save prompt dialog" )];
		[alert addButtonWithTitle:NSLocalizedString( @"Cancel", @"Label for cancel button in save prompt dialog" )];
		NSButton *button;
		button = [alert addButtonWithTitle:NSLocalizedString( @"Don't Apply", 
															  @"Label for dont' apply button in save prompt dialog" )];
		[button setKeyEquivalent:@"d"];
		[button setKeyEquivalentModifierMask:NSCommandKeyMask];
		[alert beginSheetModalForWindow:[[self mainView] window]
						  modalDelegate:self 
						 didEndSelector:@selector(applySheetDidEnd:returnCode:contextInfo:) 
							contextInfo:nil];
		return NSUnselectLater;
	}
	else
	{
		return NSUnselectNow;
	}
}

// ---------------------------------------------------------------------------
// willUnselect
//
// NSPreferencePane delegate method
// ---------------------------------------------------------------------------
- (void)willUnselect
{
	// NOTE: docs say default impl does nothing, so not necessary to call 
	// [super willUnselect];

	// We could be unselected, then reselected, so there are a few objects
	// where we need to go ahead and disconnect them and then release
	// them so we can re-create if we're reloaded.
	[ipcTimer invalidate];
	[ipcTimer autorelease];
	ipcTimer = nil;
	[logTimer invalidate];
	[logTimer autorelease];
	logTimer = nil;
	
	if( nil != serverProxy )
	{
		@try
		{
			[serverProxy unregisterClientId:clientIdent];
		}
		@catch( NSException *exception )
		{
			NSLog(@"willUnselect caught %@: %@", 
					[exception name], [exception  reason]);
		}
		@finally
		{
			[serverProxy autorelease];
			serverProxy = nil;
		}
	}
	
	// Flush our prefs
	CFPreferencesAppSynchronize( CFSTR(FF_PREFS_DOMAIN) );
	
	// Last, make sure the login item is set up appropriately, no matter how
	// we are getting out of here.
	[self updateLoginItem];
}

// ===========================================================================
// Functions to handle user input and interaction
// ===========================================================================

// ---------------------------------------------------------------------------
// browseButtonClicked:
//
// User wants to change the library location.  Pop up an "Open" sheet
// ---------------------------------------------------------------------------
- (IBAction)browseButtonClicked:(id)sender
{
	NSOpenPanel *panel = [NSOpenPanel openPanel];
	[panel setCanChooseDirectories:YES];
	[panel setCanChooseFiles:NO];
	[panel setResolvesAliases:YES];
	[panel setPrompt:NSLocalizedString( @"Choose", @"The Choose button in the library browser dialog" )];
	[panel setTitle:NSLocalizedString( @"Choose Library Location", @"Title of the library browser dialog" )];
	[panel setMessage:NSLocalizedString( 
						@"Please select the folder containing your music library, then click Choose.",
						@"Info text for the library browse dialog" )];
	NSString *path = [@"~/" stringByExpandingTildeInPath];	// default
	NSString *file = nil;
	NSFileManager *mgr = [NSFileManager defaultManager];
	BOOL bIsDir = NO;
	if( [mgr fileExistsAtPath:libraryPath isDirectory:&bIsDir] && bIsDir )
	{
		file = [libraryPath lastPathComponent];
		path = [libraryPath stringByDeletingLastPathComponent];
	}
	
	[panel beginSheetForDirectory:path 
							 file:file 
							types:nil 
				   modalForWindow:[[self mainView] window] 
					modalDelegate:self
				   didEndSelector:@selector(browsePanelEnded:returnCode:contextInfo:)
					  contextInfo:nil];
}
		
// ---------------------------------------------------------------------------
// browsePanelEnded:returnCode:contextInfo:
//
// Delegate method for the "Open" sheet.  Handle the user's choice.
// ---------------------------------------------------------------------------
- (void)browsePanelEnded:(NSOpenPanel *)panel returnCode:(int)panelResult contextInfo:(void *)contextInfo
{
	if( NSOKButton == panelResult )
	{
		NSArray *selectedDirArray = [panel filenames];
		if( 0 < [selectedDirArray count] )
		{
			[libraryPath setString:[selectedDirArray objectAtIndex:0]];
			[libraryField setStringValue:libraryPath];
			[self setIconForPath];
			[self setConfigNeedsSaving:YES];
		}
	}
}

// ---------------------------------------------------------------------------
// passwordChanged:
// ---------------------------------------------------------------------------
- (IBAction)passwordChanged:(id)sender
{
	if( NSOrderedSame != [serverPassword compare:[passwordField stringValue]] )
	{
		[serverPassword setString:[passwordField stringValue]];
		[self setConfigNeedsSaving:YES];
	}
}

// ---------------------------------------------------------------------------
// shareNameChanged:
// ---------------------------------------------------------------------------
- (IBAction)shareNameChanged:(id)sender
{
	if( NSOrderedSame != [serverName compare:[nameField stringValue]] )
	{
		[serverName setString:[nameField stringValue]];
		[self setConfigNeedsSaving:YES];
	}
}


// ---------------------------------------------------------------------------
// portPopupChanged:
// ---------------------------------------------------------------------------
- (IBAction)portPopupChanged:(id)sender
{
	if( 0 == [portPopup indexOfSelectedItem] )
	{
#if 0
		[portField abortEditing];
		[portField setIntValue:currentServerPort];
		[portField setEnabled:false];
#endif
	}
	else
	{
		[portField setEnabled:true];
		[[[self mainView] window] makeFirstResponder:portField];
	}
	[self setConfigNeedsSaving:YES];
}

// ---------------------------------------------------------------------------
// portChanged:
//
// The value of the port changed
// ---------------------------------------------------------------------------
- (IBAction)portChanged:(id)sender
{
	if( serverPort != [portField intValue] )
	{
		serverPort = [portField intValue];
		[self setConfigNeedsSaving:YES];
	}
}

// ---------------------------------------------------------------------------
// pwCheckBoxChanged:
//
// User changed the state of the "Require Password" checkbox.
// ---------------------------------------------------------------------------
- (IBAction)pwCheckBoxChanged:(id)sender
{
	if( NSOffState == [passwordCheckbox state] )
	{
		[passwordField validateEditing];
		[passwordField setStringValue:@""];
		[serverPassword setString:@""];
		[passwordField setEnabled:false];
		[self setConfigNeedsSaving:YES];
	}
	else
	{
		[passwordField setEnabled:true];
		[[[self mainView] window] makeFirstResponder:passwordField];
		if( 0 < [serverPassword length] )
			[self setConfigNeedsSaving:YES];  // Only enable if there's a password
	}
}

// ---------------------------------------------------------------------------
// serverStartOptionChanged:
//
// User changed the popup menu of server options.
// ---------------------------------------------------------------------------
- (IBAction)serverStartOptionChanged:(id)sender
{
	bStartServerOnLogin = ( 1 == [serverStartOptions indexOfSelectedItem] );
	[self setConfigNeedsSaving:YES];
}

// ---------------------------------------------------------------------------
// startStopButtonClicked:
//
// Start or stop the server.
// ---------------------------------------------------------------------------
- (IBAction)startStopButtonClicked:(id)sender
{
	if( ![self fireflyIsRunning] )
	{
		// Server is not running, so we need to start it.  First, let's see
		// if we have unsaved changes
		BOOL bOKToStart = !bConfigNeedsSaving;
		if( bConfigNeedsSaving &&
			[[[self mainView] window] makeFirstResponder:[[self mainView] window]] &&
			[self currentTabIsValid] )
		{
			if( [self saveSettings] )
			{
				[applyNowButton setEnabled:NO];
				bOKToStart = YES;
			}
			else
			{
				NSBeginCriticalAlertSheet( NSLocalizedString( @"Failed to save changes",
															  @"Alert message notifying the user of failure to save" ),
										   @"OK", 
										   NULL, 
										   NULL, 
										   [[self mainView] window], 
										   nil, 
										   NULL, 
										   NULL, 
										   NULL, 
										   NSLocalizedString( @"Firefly could not be started because your changes "
															  "could not be saved",
															  @"Explanatory text for the failure-to-save alert" ) );
			}
		}
		
		if( bOKToStart )
		{
			[self updateServerStatus:kFireflyStatusStarting];
			if( ![self startFirefly] )
			{
				[self updateServerStatus:kFireflyStatusInvalid];
				NSBeginCriticalAlertSheet( NSLocalizedString( @"Unable to start Firefly",
															  @"Alert message notifying the user of failure to stop" ),
										   @"OK", 
										   NULL, 
										   NULL, 
										   [[self mainView] window], 
										   nil, 
										   NULL, 
										   NULL, 
										   NULL, 
										   NSLocalizedString( @"An unexpected error occurred when trying to start Firefly. "
															  "Please close and re-open this Preference pane, and try again.",
															  @"Explanatory text for the failure-to-stop alert" ) );
			}			
		}
	}
	else
	{
		// Server is running, so stop it.
		if( [self stopFirefly] )
		{
			[self updateServerStatus:kFireflyStatusStopping];
		}
		else
		{
			[self updateServerStatus:kFireflyStatusInvalid];
			NSBeginCriticalAlertSheet( NSLocalizedString( @"Unable to stop Firefly",
														  @"Alert message notifying the user of failure to stop" ),
									   @"OK", 
									   NULL, 
									   NULL, 
									   [[self mainView] window], 
									   nil, 
									   NULL, 
									   NULL, 
									   NULL, 
									   NSLocalizedString( @"An unexpected error occurred when trying to stop Firefly. "
														  "Please close and re-open this Preference pane, and try again.",
														  @"Explanatory text for the failure-to-stop alert" ) );
		}			
	}
}


// ---------------------------------------------------------------------------
// webPageButtonClicked:
//
// User clicked the Open Web Page button, so we want to open the server's
// config page.
// ---------------------------------------------------------------------------
- (IBAction)webPageButtonClicked:(id)sender
{
	// User clicked the Show web page button.  Open the firefly internal page.
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:serverURL]];
}

// ---------------------------------------------------------------------------
// applyNowButtonClicked:
//
// Time to save our settings!
// ---------------------------------------------------------------------------
- (IBAction)applyNowButtonClicked:(id)sender
{
	if( [[[self mainView] window] makeFirstResponder:[[self mainView] window]] &&
		[self currentTabIsValid] )
	{
		if( [self saveSettings] )
		{
			[applyNowButton setEnabled:NO];
			if( [self fireflyIsRunning] )
				[self restartFirefly];
		}
		else
			NSBeginCriticalAlertSheet( NSLocalizedString( @"Failed to apply changes",
														  @"Alert message notifying the user of failure to apply" ),
									   @"OK", 
									   NULL, 
									   NULL, 
									   [[self mainView] window], 
									   nil, 
									   NULL, 
									   NULL, 
									   NULL, 
									   NSLocalizedString( @"Due to an unexpected error, your changes could not "
														  "be applied.",
														  @"Explanatory text for the failure-to-apply alert" ) );
	}
}

// ---------------------------------------------------------------------------
// helperMenuCheckboxClicked:
//
// User clicked the checkbox to show or hide the firefly menu.  This happens
// right away.  The helper writes the pref for us.
// ---------------------------------------------------------------------------
- (IBAction)helperMenuCheckboxClicked:(id)sender
{
	if( NSOffState == [helperMenuCheckbox state] )
		[self showHelperMenu:NO];
	else
		[self showHelperMenu:YES];
}

// ---------------------------------------------------------------------------
// logoButtonClicked:
//
// User clicked the logo button, so we want to open the Firefly web site
// ---------------------------------------------------------------------------
- (IBAction)logoButtonClicked:(id)sender
{
	// User clicked the Firefly logo in the prefs pane.  Open the web page.
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"http://fireflymediaserver.org"]];
}


// ---------------------------------------------------------------------------
// controlTextDidChange:
//
// If the text in the control changes at all (the first time a user adds
// or removes a character), we want to mark the configuration as needing
// to be saved
// ---------------------------------------------------------------------------
- (void)controlTextDidChange:(NSNotification *)notification
{
	// If any of our text fields have changed text, we need to enable the "Apply" button.
	[self setConfigNeedsSaving:YES];
}


// ---------------------------------------------------------------------------
// validateInstall:
//
// Called when the prefs pane is first being loaded.  Locates the pieces we
// need to do our work (specifically, the config file and the Firefly Helper
// application).  Creates the Firefly directory and a default config file
// if none is present.  Makes note of any errors encountered for reporting
// to the user when the panel finishes loading.
// ---------------------------------------------------------------------------
- (BOOL)validateInstall
{
	configAppearsValid = NO;
	
	do // while( false )
	{
		// First up, locate or create the Firefly directory in Application Support.
		NSFileManager *mgr = [NSFileManager defaultManager];
		NSArray * appSupportDirArray = nil;
		NSString * appSupportPath = nil;
		
		// If we were guaranteed to be on 10.4 or later, we could call this:
		//appSupportDirArray = NSSearchPathForDirectoriesInDomains( NSApplicationSupportDirectory, 
		//														  NSUserDomainMask, 
		//														  YES );
		
		// But, we're not on 10.4; we have to go back to 10.3.  So, we look in 
		// the Library directory.
		appSupportDirArray = NSSearchPathForDirectoriesInDomains( NSLibraryDirectory,
																  NSUserDomainMask,
																  YES );
		if( [appSupportDirArray count] > 0 )
		{
			appSupportPath = [[appSupportDirArray objectAtIndex:0] 
								stringByAppendingPathComponent:@"Application Support"];
		}
		else
		{
			[configError setString:NSLocalizedString( @"Library directory could not be found in user folder",
													  @"Error message displayed at panel load" )];
			break;
		}
		
		BOOL isDir = YES;
		if( ![mgr fileExistsAtPath:appSupportPath isDirectory:&isDir] || !isDir )
		{
			BOOL bFail = YES;
			// If this is still true, it means that the directory is missing
			// (Otherwise, there's a *file* called Application Support here!
			if( isDir )
			{
				bFail = ( 0 != mkdir( [mgr fileSystemRepresentationWithPath:appSupportPath],
									  0755 ) );
			}
			
			if( bFail )
			{
				[configError setString:NSLocalizedString( @"Unable to find or create Application Support folder",
														  @"Error message displayed at panel load" )];
				break;
			}				
		}

		[fireflyFolderPath setString:[appSupportPath stringByAppendingPathComponent:@FIREFLY_DIR_NAME]];
		if( ![mgr fileExistsAtPath:fireflyFolderPath isDirectory:&isDir] || !isDir )
		{
			// As above, except that this is less unexpected
			BOOL bFail = YES;
			// If this is still true, it means that the directory is missing
			// (Otherwise, there's a *file* called Application Support here!
			if( isDir )
			{
				bFail = ( 0 != mkdir( [mgr fileSystemRepresentationWithPath:fireflyFolderPath],
									  0755 ) );
			}
			
			if( bFail )
			{
				// We're done.  If we can't find the Firefly directory, notify the user and disable
				// everything.  Yes, maybe the server might be running and we could locate it, but somebody
				// who has installed the server in a non-standard location doesn't need us.
				NSString *formatString = NSLocalizedString( @"Firefly directory could not be found or created at: %@",
															"Format string for error message" );
				[configError setString:[NSString stringWithFormat:formatString, fireflyFolderPath]];
				break;
			}				
		}
		
		// Check for the config file
		[configFilePath setString:[fireflyFolderPath stringByAppendingPathComponent:@FIREFLY_CONF_NAME]];
		if( [mgr fileExistsAtPath:configFilePath] )
		{
			// It exists.  Can we write to it?
			if( ![mgr isWritableFileAtPath:configFilePath] )
			{
				// This is bad.  If we can't write to the config file, all we can do is open the web page and
				// start/stop the server
				NSString *formatString = NSLocalizedString( @"The configuration file is present, but is not writable: %@",
															"Format string for error message" );
				[configError setString:[NSString stringWithFormat:formatString, configFilePath]];
				break;
			}
		}
		else
		{
			// No config file, so let's create the default one
			if( ![self createDefaultConfigFile] )
			{
				// Fatal error.  Alert the user and disable everything.
				NSString *formatString = NSLocalizedString( @"Unable to create a default configuration file at: %@",
															"Format string for error message upon invalid install" );
				[configError setString:[NSString stringWithFormat:formatString, configFilePath]];
				break;
			}
			
			// This lets willSelect know that we wrote a new config file
			bConfigNeedsSaving = YES;
		}
		
		// Check to make sure the helper app is present (also required)
		[fireflyHelperPath setString:[[NSBundle bundleForClass:[self class]] pathForResource:@FIREFLY_HELPER_NAME
																					  ofType:nil]];
		if( ![mgr isExecutableFileAtPath:fireflyHelperPath] )
		{
			// As above, this is a fatal error
			[configError setString:NSLocalizedString( @"The Firefly installation appears to be damaged.  Unable to"
													  " locate Firefly Helper.",
													  @"Format string for error message upon invalid install" )];
			break;
		}
			
		// Phew!
		configAppearsValid = YES;

	} while( false );
	
	return configAppearsValid;
}

// ---------------------------------------------------------------------------
// readConfigFromPath:
//
// Reading and writing the config file
// ---------------------------------------------------------------------------
- (BOOL)readConfigFromPath:(NSString*)path
{
	// I'm sure there's a nice Cocoa/Carbon/MacOS way to do this, but the docs
	// are not forthcoming.  So, we do it the Unix way.
	FILE *configFile = fopen( [path UTF8String], "r" );
	if( NULL == configFile )
		return NO;
	
	// Set up our members in case we've been run before
	idxOfServerName = 0;
	idxOfPassword = 0;
	idxOfPort = 0;
	idxOfLibraryPath = 0;
	idxOfNextSection = 0;
	idxOfDbPath = 0;
	idxOfLogPath = 0;
	
	// Now, read the file
	BOOL bInGeneral = NO;
	char buf[1024]; // yes, a hardcoded limit, but seriously, this is for one line.
	[configFileStrings removeAllObjects];
	
	while( NULL != fgets( buf, 1024, configFile ) )
	{
		buf[1023] = 0;
		NSString *line = [NSString stringWithUTF8String:buf];
		[configFileStrings addObject:line];
		
		// Check to see if this is one of the lines we care about
		if( 0 == idxOfNextSection )
		{
			if( bInGeneral )
			{
				if( 0 == idxOfServerName && 0 == strncasecmp( buf, "servername", 10 ) )
				{
					idxOfServerName = [configFileStrings count] - 1;
					[serverName setString:[self readValueFromBuf:buf startingAt:10 unescapeCommas:NO]];
				}
				else if( 0 == idxOfPassword && 0 == strncasecmp( buf, "password", 8 ) )
				{
					idxOfPassword = [configFileStrings count] - 1;
					[serverPassword setString:[self readValueFromBuf:buf startingAt:8 unescapeCommas:NO]];
				}
				else if( 0 == idxOfPort && 0 == strncasecmp( buf, "port", 4 ) )
				{
					idxOfPort = [configFileStrings count] - 1;
					NSString *tmp = [self readValueFromBuf:buf startingAt:4 unescapeCommas:NO];
					unsigned long num = atol( [tmp UTF8String] );
					if( num < 65536 )
						serverPort = num;
				}
				else if( 0 == idxOfLibraryPath && 0 == strncasecmp( buf, "mp3_dir", 7 ) )
				{
					idxOfLibraryPath = [configFileStrings count] - 1;
					[libraryPath setString:[self readValueFromBuf:buf startingAt:7 unescapeCommas:YES]];
				}
				else if( 0 == idxOfDbPath && 0 == strncasecmp( buf, "db_parms", 8 ) )
				{
					// We only save the index of this if we're going to need to write the path
					// out.  We need to write it out if the string is empty (which means that
					// it's coming from a default file).
					NSString *string = [self readValueFromBuf:buf startingAt:8 unescapeCommas:NO];
					if( 0 == [string length] )
						idxOfDbPath = [configFileStrings count] - 1;
				}
				else if( 0 == idxOfLogPath && 0 == strncasecmp( buf, "logfile", 7 ) )
				{
					// as above
					[logFilePath setString:[self readValueFromBuf:buf startingAt:7 unescapeCommas:NO]];
					if( 0 == [logFilePath length] )
						idxOfLogPath = [configFileStrings count] - 1;
				}
				else if( 0 == idxOfPlaylistPath && 0 == strncasecmp( buf, "playlist", 8 ) )
				{
					// as above
					NSString *string = [self readValueFromBuf:buf startingAt:8 unescapeCommas:NO];
					if( 0 == [string length] )
						idxOfPlaylistPath = [configFileStrings count] - 1;
				}
				
				else if( buf[0] == '[' )
				{
					idxOfNextSection = [configFileStrings count] - 1;
				}
			}
			else
			{
				if( 0 == strncasecmp( buf, "[general]", 9 ) )
					bInGeneral = YES;
			}
		}
	}
	
	fclose( configFile );
	return YES;
}

// ---------------------------------------------------------------------------
// WriteCommaEscapedStringToFile:
//
// Utility function.  Takes a const char* as input and copies the string,
// escaping any commas as ",,".  Writes the resulting string to the
// supplied FILE*, which is assumed to be open for writing
//
// NOTE: This function could be smarter.  For example, if a string 
// is passed in that is 1023 characters in length but contains commas,
// this function will truncate the string silently.
// ---------------------------------------------------------------------------
static void
WriteCommaEscapedStringToFile( FILE *inFile, const char *inStringToEscape )
{
	if( NULL == inFile || NULL == inStringToEscape )
		return;
	
	char escapingBuf[1025];  // 1 extra in case of a final comma
	int i = 0;
	int j = 0;
	while( '\0' != inStringToEscape[i] && j < 1023 )
	{
		// Extra comma for any comma we find
		if( ',' == inStringToEscape[i] )
			escapingBuf[j++] = ',';
		escapingBuf[j++] = inStringToEscape[i++];
	}
	escapingBuf[j] = '\0';
	fputs( escapingBuf, inFile );
}

// ---------------------------------------------------------------------------
// writeConfigToPath:
//
// Writes our configuration to the supplied path.  readConfigFromPath
// MUST have been called first!
// ---------------------------------------------------------------------------
- (BOOL)writeConfigToPath:(NSString*)path
{
	if( nil == configFileStrings || 0 == [configFileStrings count] )
		return NO;
	
	FILE *configFile = fopen( [path UTF8String], "w" );
	if( NULL == configFile )
		return NO;
	
	char buf[1024];
	
	unsigned i = 0;
	for( ; i < [configFileStrings count]; i++ )
	{
		if( 0 == i )
		{
			// 0 is special-cased since it's also a special token for "this line wasn't 
			// in the original".  Since we only use the new file format, though, we're guaranteed
			// that none of our tokens is at the first (0th) line, because [general] has to
			// come before any of our tokens
			fputs( [[configFileStrings objectAtIndex:0] UTF8String], configFile );
		}
		else if( i == idxOfNextSection )
		{
			// We've reached the end of our general section, so it's now time to write 
			// out anything that the user has set, but that wasn't found in the config 
			// file before.  Note that this is basically error recovery for somebody
			// mucking with the file, since all lines should be present but with emtpy
			// values if an optional setting isn't set.  For example: 
			//    password =
			// If we didn't find it before, its index will be 0.
						
			// servername is required
			if( 0 == idxOfServerName )
			{
				sprintf( buf, "servername = %s\n", [serverName UTF8String] );
				fputs( buf, configFile );
			}
			
			// so is the library path
			if( 0 == idxOfLibraryPath )
			{
				sprintf( buf, "mp3_dir = %s\n", [libraryPath UTF8String] );
				WriteCommaEscapedStringToFile( configFile, buf );
			}
			
			// password and port are optional, so don't write an empty entry
			if( 0 == idxOfPassword && [serverPassword length] > 0 )
			{
				sprintf( buf, "password = %s\n", [serverPassword UTF8String] );
				fputs( buf, configFile );
			}
			if( 0 == idxOfPort && 0 != serverPort )
			{
				sprintf( buf, "port = %u\n", serverPort );
				fputs( buf, configFile );
			}
			
			// Don't forget the section header for that next section!
			fputs( [[configFileStrings objectAtIndex:i] UTF8String], configFile );
		}
		else if( i == idxOfServerName )
		{
			sprintf( buf, "servername = %s\n", [serverName UTF8String] );
			fputs( buf, configFile );
		}
		else if( i == idxOfPassword ) // NOTE: This will write an empty password if none is set
		{
			sprintf( buf, "password = %s\n", [serverPassword UTF8String] );
			fputs( buf, configFile );
		}
		else if( i == idxOfPort )
		{
			if( 0 == serverPort )
				fputs( "port =\n", configFile ); // no port is set, so write an empty value
			else
			{
				sprintf( buf, "port = %u\n", serverPort );
				fputs( buf, configFile );
			}
		}
		else if( i == idxOfLibraryPath )
		{
			sprintf( buf, "mp3_dir = %s\n", [libraryPath UTF8String] );
			WriteCommaEscapedStringToFile( configFile, buf );
		}
		else if ( i == idxOfDbPath )
		{
			sprintf( buf, "db_parms = %s\n", [fireflyFolderPath UTF8String] );
			WriteCommaEscapedStringToFile( configFile, buf );
		}
		else if( i == idxOfLogPath )
		{
			sprintf( buf, "logfile = %s\n", [logFilePath UTF8String] );
			WriteCommaEscapedStringToFile( configFile, buf );
		}
		else if( i == idxOfPlaylistPath )
		{
			sprintf( buf, "playlist = %s\n", [playlistPath UTF8String] );
			WriteCommaEscapedStringToFile( configFile, buf );
		}
		else
		{
			// Just output our stored line
			fputs( [[configFileStrings objectAtIndex:i] UTF8String], configFile );
		}
	}
	
	fclose( configFile );
	[self setConfigNeedsSaving:NO];
	return YES;
}


// ---------------------------------------------------------------------------
// readValueFromBuf:startingAt:unescapeCommas:
//
// Read the value from a key/value pair string.  idx is the start point, which
// is assumed to be the next character after the key.  NOTE that this function 
// may modify buf if it contains trailing whitespace.
// ---------------------------------------------------------------------------
- (NSString *)readValueFromBuf:(char*)buf startingAt:(int)idx unescapeCommas:(BOOL)bUnescapeCommas
{
	char *retVal = NULL;
	BOOL bFoundEquals = NO;
	
	// skip over whitespace and = characters
	while( buf[idx] )
	{
		if( buf[idx] == '=' )
			bFoundEquals = YES;
		else if( buf[idx] != ' ' && buf[idx] != '\t' )
			break;
		
		idx++;
	}

	// Okay, we found whitespace or the end of the line.  If we didn't find an equals sign, then the
	// value is empty.  If there's nothing there but a newline, that's also considered empty.
	if( !bFoundEquals || buf[idx] == '\0' || buf[idx] == '\n' || buf[idx] == '\r' )
		return [NSString string];

	// We found an equals, so retVal will point at our string to return.  Now it's time
	// to clip off any trailing whitespace.  We work back from the end of the line, since
	// whitespace is permitted in some places like the path and server name.
	
	retVal = &buf[idx];
	idx = strlen( retVal ); // this is at least 1, because we tested for empty above
	
	while( idx-- && (retVal[idx] == ' ' || retVal[idx] == '\t' || retVal[idx] == '\r' || retVal[idx] == '\n') )
		retVal[idx] = '\0';
	
	// And, finally, if bUnescapeCommas is true, walk the string and 
	// convert ",," to "," in place
	if( bUnescapeCommas)
	{
		int readIdx = 0;
		int writeIdx = 0;
		while( '\0' != retVal[readIdx] )
		{
			if( ',' == retVal[readIdx] && ',' == retVal[readIdx+1] )
				readIdx++;
			retVal[writeIdx++] = retVal[readIdx++];
		}
		retVal[writeIdx] = '\0';
	}
	
	return [NSString stringWithUTF8String:retVal];
}

// ---------------------------------------------------------------------------
// createDefaultConfigFile:
//
// Read the value from a key/value pair string.  idx is the start point, which
// is assumed to be the next character after the key.  NOTE that this function 
// may modify buf if it contains trailing whitespace.
// ---------------------------------------------------------------------------
- (BOOL)createDefaultConfigFile
{
	// Read the default file from our bundle
	NSString *configPath = nil;
	NSBundle *thisBundle = [NSBundle bundleForClass:[self class]];
	if( nil == (configPath = [thisBundle pathForResource:@FIREFLY_CONF_NAME ofType:nil]) ||
		![self readConfigFromPath:configPath] )
		return NO;
	
	// Set the default values.  This takes the items that are deliberately
	// left blank in the "starter" config file (because they're specific 
	// to the particular installation location) and fills them in.
	[self setDefaultValues];
	
	// Write it out
	return [self writeConfigToPath:configFilePath];
}

// ---------------------------------------------------------------------------
// setDefaultValues:
//
// Model utility sets up the members representing server configuration to
// their proper initial defaults (which are host- and user-specific!)
// ---------------------------------------------------------------------------
- (void)setDefaultValues
{
	// easy ones first
	serverPort = 0;
	[serverPassword setString:@""]; // FIXME: really no better way to clear a string?
	bStartServerOnLogin = YES;
	
	// Get the host name and make the default library name in a localization-friendly way
	NSString *hostname = (NSString*)CSCopyMachineName();
	NSString *format = NSLocalizedString( @"%@'s Firefly on %@", 
										  @"Format string for default library name" );
		
	[serverName setString:[NSString stringWithFormat:format, userName, hostname]];
	[hostname release];
	
	// Defaults for the log file and playlist paths.  These get used only
	// when we first write out our default config file.
	[logFilePath setString:[fireflyFolderPath stringByAppendingPathComponent:
		@FIREFLY_LOG_FILE]];
	[playlistPath setString:[fireflyFolderPath stringByAppendingPathComponent:
		@FIREFLY_PLAYLIST_FILE]];
	
	// Finally, the default Music directory
	[libraryPath setString:[@"~/Music" stringByExpandingTildeInPath]];
}


// ---------------------------------------------------------------------------
// setConfigNeedsSaving:
//
// Tracking the need to save the config.
// ---------------------------------------------------------------------------
-(void)setConfigNeedsSaving:(BOOL)needsSaving
{
	[applyNowButton setEnabled:needsSaving];
	bConfigNeedsSaving = needsSaving;
}

// ===========================================================================
// UI utility functions for setting the UI into certain common states
// (sets up control enabling, text, etc.)
// ===========================================================================

// ---------------------------------------------------------------------------
// disableAllControls:
//
// Disables all the controls in the prefs pane.  Used when the configuration
// is invalid.
// ---------------------------------------------------------------------------
- (void)disableAllControls
{
	[browseButton setEnabled:false];
	[libraryField setEnabled:false];
	[nameField setEnabled:false];
	[passwordCheckbox setEnabled:false];
	[passwordField setEnabled:false];
	[portField setEnabled:false];
	[serverStartOptions setEnabled:false];
	[startStopButton setEnabled:false];
	[webPageButton setEnabled:false];
	[helperMenuCheckbox setEnabled:false];
}

// ---------------------------------------------------------------------------
// updateServerStatus:
//
// Handles updating all relevant UI elements according to the server status
// ---------------------------------------------------------------------------
- (void)updateServerStatus:(FireflyServerStatus) status
{
	BOOL bAnimateProgress = NO;
	BOOL bActivateStartStop = YES;
	BOOL bButtonIsStart = YES;
	BOOL bClearWebAndVersion = NO;
	
	switch( status )
	{
		case kFireflyStatusStartFailed:
		case kFireflyStatusCrashed:
		case kFireflyStatusStopped:
			bClearWebAndVersion = YES;
			break;

		case kFireflyStatusRestarting:
		case kFireflyStatusStarting:
			bAnimateProgress = YES;
			bButtonIsStart = NO;
			break;
			
		case kFireflyStatusActive:
		case kFireflyStatusScanning:
			bButtonIsStart = NO;
			break;
		
		case kFireflyStatusStopping:
			bActivateStartStop = NO;
			bAnimateProgress = YES;
			break;
		
		case kFireflyStatusInvalid:
		default:
			bActivateStartStop = NO;
			bClearWebAndVersion = YES;
			break;
	}
	
	[startStopButton setEnabled:bActivateStartStop];
	[statusText setStringValue:StringForFireflyStatus(status)];
	if( bAnimateProgress )
		[progressSpinner startAnimation:self];
	else
		[progressSpinner stopAnimation:self];
	
	if( bButtonIsStart )
		[startStopButton setTitle:NSLocalizedString( @"Start Firefly", 
													 @"One of several titles for the start/stop button" )];
	else
		[startStopButton setTitle:NSLocalizedString( @"Stop Firefly", 
													 @"One of several titles for the start/stop button" )];
	
	if( bClearWebAndVersion )
	{
		[serverVersionText setStringValue:NSLocalizedString( @"(available when Firefly is running)",
															 @"Displayed in place of server version when server "
															 "is not running" )];
		[webPageButton setEnabled:NO];
		[webPageInfoText setStringValue:NSLocalizedString( @"Additional configuration options are "
														   "available from Firefly's built-in web page. "
														   "Available when Firefly is running.",
														   @"Info text for the web page button when server "
														   "is not running" )];
	}
}
	

// ===========================================================================
// Alert delegate method(s)
// ===========================================================================

// ---------------------------------------------------------------------------
// alertDidEnd:returnCode:contextInfo:
//
// This is called for our "OK"-type alerts, and we don't need to do anything
// extra.
// ---------------------------------------------------------------------------
- (void)alertDidEnd:(NSAlert *)alert returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
}

// ---------------------------------------------------------------------------
// applySheetDidEnd:returnCode:contextInfo:
//
// Sheet delegate method, specially for the "apply changes" sheet.  Depending
// upon the user's answer, we may need to write out our config file and
// restart the server.  We definitely have to send the replyToShouldUnselect
// message, since we deferred a reply in shouldUnselect, and that's what 
// prompts this sheet.
// ---------------------------------------------------------------------------
- (void) applySheetDidEnd:(NSAlert *)alert returnCode:(int)returnCode contextInfo:(void *)contextInfo
{
	BOOL bResponse = YES;  // what do we say to shouldUnselect?
	
	// We want the sheet closed in case we have to post another alert
	[[alert window] orderOut:self];
	
	if( NSAlertSecondButtonReturn == returnCode )		// "Cancel" button
	{
		bResponse = NO;
	}
	else if( NSAlertThirdButtonReturn == returnCode )	// "Don't Apply" button
	{
		// bResponse is already YES
	}
	else if( NSAlertFirstButtonReturn == returnCode )	// "Apply" button
	{
		if( [self currentTabIsValid] )
		{
			bResponse = YES;
			if( ![self saveSettings] )
			{
				bResponse = NO;
				NSBeginCriticalAlertSheet( NSLocalizedString( @"Failed to apply changes",
															  @"Alert message notifying the user of failure to apply" ),
										   @"OK", 
										   NULL, 
										   NULL, 
										   [[self mainView] window], 
										   nil, 
										   NULL, 
										   NULL, 
										   NULL, 
										   NSLocalizedString( @"Due to an unexpected error, your changes could not "
															  "be applied.",
															  @"Explanatory text for the failure-to-apply alert" ) );
			}
			else
			{
				// If the server is running, we need to restart it.  Happily,
				// the Firefly Helper will take care of that, so we can
				// go ahead and exit
				if( [self fireflyIsRunning] )
					[self restartFirefly];
			}
		}
		else
		{
			// Our tab data wasn't valid, so now there's an alert on the
			// screen.  Cancel closing the sheet.
			bResponse = NO;
		}
	}
	
	[self replyToShouldUnselect:bResponse];
}

// ===========================================================================
// Tab view delegate method(s)
// ===========================================================================

// ---------------------------------------------------------------------------
// -tabView:shouldSelectTabViewItem:(NSTabViewItem *)tabViewItem
//
// Our job is to return false if the tab view should not be able to switch
// panes.  We shouldn't switch if there's invalid text in any of our
// fields.  By trying to get the main window to become first responder, we
// make sure that any field with editing in process will call its delegate
// to see if the field value is valid.  If it's not, then it won't be able
// to give up first responder status, and makeFirstResponder will return
// false.
// ---------------------------------------------------------------------------
- (BOOL)tabView:(NSTabView *)tabView shouldSelectTabViewItem:(NSTabViewItem *)tabViewItem
{
	BOOL bRetVal = [[[self mainView] window] makeFirstResponder:[[self mainView] window]];
	if( bRetVal )
		bRetVal = [self currentTabIsValid];
	
	if( bRetVal )
	{
		// See whether we need to start or stop our log update timer
		if( 3 == [tabView numberOfTabViewItems] )
		{
			NSTabViewItem *logTab = [tabView tabViewItemAtIndex:2];
			if( [tabViewItem isEqual:logTab] )
			{
				// Update the view and start the timer
				[self updateLogTextView];
				logTimer = [[NSTimer scheduledTimerWithTimeInterval:1.0
															 target:self
														   selector:@selector(logTimerFired:)
														   userInfo:nil
															repeats:YES] retain];
				
			}
			else if( [[tabView selectedTabViewItem] isEqual:logTab] )
			{
				// stop timer
				[logTimer invalidate];
				[logTimer autorelease];
				logTimer = nil;
			}
		}
	}
	return bRetVal;
}

// ---------------------------------------------------------------------------
// control:isValidObject
//
// NSControl delegate method, called when a control is about to commit its
// newly-edited value.  We return NO if the new value is not allowed, so
// it will not allow editing to leave.
// ---------------------------------------------------------------------------
- (BOOL)control:(NSControl *)control isValidObject:(id) obj
{
	BOOL bRetVal = YES;
	if( [obj isKindOfClass:[NSString class]] )
	{
		NSString *string = (NSString *)obj;
		if( control == portField )
		{
			bRetVal = ( 1023 < [string intValue] && 65536 > [string intValue] );
		}
		else if( control == nameField )
		{
			bRetVal = ( 0 < [string length] );
		}
		else if( control == passwordField )
		{
			bRetVal = ( NSOffState == [passwordCheckbox state] || 0 < [string length] );
		}
	}
	
	if( NO == bRetVal )
		[self alertForControl:control];
	
	return bRetVal;
}

// ---------------------------------------------------------------------------
// currentTabIsValid
//
// Here's the deal.  Annoyingly, Cocoa text fields don't call their delegate
// methods when they lose focus *if* they haven't changed.  So, this method
// is our backstop, in case the other delegates don't catch this case.  It
// figures out the current tab, and then checks that the fields are valid.
//
// NOTE: We assume that the window has already been set to the first
// responder, so that we can query the controls directly for their values.
// ---------------------------------------------------------------------------
- (BOOL)currentTabIsValid
{
	BOOL bRetVal = YES;
	NSTabViewItem *selectedTab = [mainTabView selectedTabViewItem];
	int idx;
	if( nil != selectedTab )
	{
		idx = [mainTabView indexOfTabViewItem:selectedTab];
		if( 0 == idx )
		{
			// General
			if( ! (bRetVal = [self control:nameField isValidObject:[nameField objectValue]]) )
				[self alertForControl:nameField];
			else if( ! (bRetVal = [self control:passwordField isValidObject:[passwordField objectValue]]) )
				[self alertForControl:nameField];
		}
		else if( 1 == idx )
		{
			// Advanced
			// If "Manual" is selected, but the value of the field is not kosher, we must say no
			if( 1 == [portPopup indexOfSelectedItem] &&
				!(bRetVal = [self control:portField isValidObject:[portField objectValue]]) )
				[self alertForControl:portField];
		}
	}
	
	return bRetVal;
}

// ========================================================================
// Private utilities
// ========================================================================

// ---------------------------------------------------------------------------
// alertForControl
//
// There are a couple of places where we may need to pop up a modal alert
// because a control's value is not valid.  So, we have this utility.  It
// displays a modal "OK" style alert sheet with text specific to the
// control, then returns.
// ---------------------------------------------------------------------------
- (void)alertForControl:(NSControl *)control
{
	NSString *alertTitle;
	NSString *alertMessage;
	if( control == nameField )
	{
		alertTitle = NSLocalizedString( @"Missing library name", "@Alert title when library name is invalid" );
		alertMessage = NSLocalizedString( @"Please enter a library name",
										  @"Error message if library name is invalid" );
	}
	else if( control == passwordField )
	{
		alertTitle = NSLocalizedString( @"Missing password", "@Alert title when password is invalid" );
		alertMessage = NSLocalizedString( @"Please enter a password, or un-check the password checkbox",
										  @"Error message if password is empty" );
	}
	else if( control == portField )
	{
		alertTitle = NSLocalizedString( @"Invalid port number", "@Alert title when port number is invalid" );
		alertMessage = NSLocalizedString( @"Please enter a port number between 1024 and 65535, or choose "
										  "\"Automatic\" from the pop-up menu",
										  @"Error message if invalid port entered" );
	}
	else
	{
		alertTitle = NSLocalizedString( @"Invalid value", @"Generic alert string for an invalid control" );
		alertMessage = @"";
	}
	
	NSBeginAlertSheet( alertTitle,
					   @"OK", NULL, NULL, [[self mainView] window], 
					   nil, NULL, NULL, NULL, 
					   alertMessage );
}

// ---------------------------------------------------------------------------
// setIconForPath
//
// This function takes our library path and sets the icon in the Advanced tab
// to be that path's icon.  It's complicated a bit by the need to have a
// special icon in case the path can't be found.
// ---------------------------------------------------------------------------
- (void)setIconForPath
{
	NSFileManager *mgr = [NSFileManager defaultManager];
	BOOL isDir = NO;
	if( [mgr fileExistsAtPath:libraryPath isDirectory:&isDir] && isDir )
	{
		NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
		[libraryIcon setImage:[workspace iconForFile:libraryPath]];
	}
	else
	{
		// we want a default "?" image, and IconServices is kind enough to oblige
		IconRef unknownIcon;
		if( 0 == GetIconRef( kOnSystemDisk, kSystemIconsCreator, kUnknownFSObjectIcon, &unknownIcon ) )
		{
			NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(32,32)]; 
			[image lockFocus]; 
			CGRect iconRect = CGRectMake(0,0,32,32);
			PlotIconRefInContext((CGContextRef)[[NSGraphicsContext currentContext] graphicsPort], 
								 &iconRect, 
								 kAlignNone, 
								 kTransformNone, 
								 NULL /*labelColor*/, 
								 kPlotIconRefNormalFlags, 
								 unknownIcon); 
			[image unlockFocus]; 
			[libraryIcon setImage:image];
			[image release];
			ReleaseIconRef( unknownIcon );
		}
		else
		{
			[libraryIcon setImage:nil];
		}
	}
}

// ---------------------------------------------------------------------------
// loadSettings
//
// Read the config file and also fetch our server startup preference from
// MacOS's preference mechanism
// ---------------------------------------------------------------------------
- (BOOL)loadSettings
{
	[self readSettingsForHelper:&bShowHelperMenu 
					  andServer:&bStartServerOnLogin];
	return [self readConfigFromPath:configFilePath];
}

// ---------------------------------------------------------------------------
// saveSettings
//
// Writes out the config file and sets our prefs for whether we need to
// launch the server at login
//
// Returns NO if this fails.
// ---------------------------------------------------------------------------
- (BOOL)saveSettings
{
	BOOL bSuccess = NO;
	
	CFPreferencesSetAppValue( CFSTR(FF_PREFS_LAUNCH_AT_LOGIN),
							  bStartServerOnLogin ? kCFBooleanTrue : kCFBooleanFalse,
							  CFSTR(FF_PREFS_DOMAIN) );
	
	// Now the server config file
	bSuccess = [self writeConfigToPath:configFilePath];
	return bSuccess;
}

// ---------------------------------------------------------------------------
// updateLoginItem
//
// Based upon the current state of the persistent prefs (NOT our locals), 
// either set or un-set the Helper as a login item.
// 
// NOTE: If bStartOnLogin is true, or if bShowMenu is true, then
// we want the firefly helper to be in the startup items (because it handles
// both of those tasks).  But, if bShowMenu is true and bStartOnLogin
// isn't, we'll start the helper but not the server.
// ---------------------------------------------------------------------------
- (BOOL)updateLoginItem
{
	BOOL bSuccess = NO;
	NSString *scriptSource = nil;
	BOOL bStartOnLogin = NO;
	BOOL bShowMenu = NO;
	[self readSettingsForHelper:&bShowMenu andServer:&bStartOnLogin];
	if( bStartOnLogin || bShowMenu )
	{
		scriptSource = [NSString stringWithFormat:
			@"tell application \"System Events\"\n"
			"if \"Firefly Helper\" is not in (name of every login item) then\n"
			"make login item at end with properties {hidden:false, path:\"%@\"}\n"
			"end if\n"
			"end tell", 
			fireflyHelperPath];
	}
	else
	{
		scriptSource = [NSString stringWithFormat:
			@"tell application \"System Events\"\n"
			"if \"Firefly Helper\" is in (name of every login item) then\n"
			"delete (every login item whose name is \"Firefly Helper\")\n"
			"end if\n"
			"end tell\n"];
	}
	
	NSDictionary *errorDict = nil;
	NSAppleScript *myScript = [[NSAppleScript alloc] initWithSource:scriptSource];
	
	if( nil != myScript )
	{
		bSuccess = (nil != [myScript executeAndReturnError:&errorDict]);
		[myScript release];
	}
	return bSuccess;
}

// ---------------------------------------------------------------------------
// readSettingsForHelper:andServer:
//
// Utility to read the helper and server launch settings, since we need to
// do it in more than one place
// ---------------------------------------------------------------------------
- (void)readSettingsForHelper:(BOOL*)outHelper andServer:(BOOL*)outServer
{
	if( NULL != outHelper )
	{
		CFBooleanRef showHelper = 
			CFPreferencesCopyAppValue( CFSTR(FF_PREFS_SHOW_MENU_EXTRA),
									   CFSTR(FF_PREFS_DOMAIN) );
		if( nil != showHelper )
		{
			*outHelper = CFBooleanGetValue( showHelper );
			CFRelease( showHelper );
		}
		else 
		{
			// default value
			*outHelper = NO;
		}
	}
	
	if( NULL != outServer )
	{
		CFBooleanRef shouldLaunch = 
			CFPreferencesCopyAppValue( CFSTR(FF_PREFS_LAUNCH_AT_LOGIN),
									   CFSTR(FF_PREFS_DOMAIN) );
		if( nil != shouldLaunch )
		{
			*outServer = CFBooleanGetValue( shouldLaunch );
			CFRelease( shouldLaunch );
		}
		else
		{
			// default value
			*outServer = YES;
		}
	}
}

// ------------------------------------------------------------------------
// helperIsRunning 
//
// Returns YES if "Firefly Helper" is running under our UID.
// ------------------------------------------------------------------------
- (BOOL)helperIsRunning
{
	bool bRetVal = NO;
	kinfo_proc *result;
	size_t length;
	GetProcesses( &result, &length );
	
	// Okay, now we have our list of processes.  Let's find OUR copy of
	// firefly.  Note that Firefly runs as two processes, so we look
	// for the higher-numbered one.
	if( NULL != result )
	{
		int procCount = length / sizeof(kinfo_proc);
		int i = 0;
		uid_t ourUID = getuid();
		for( ; i < procCount; i++ )
		{
			if( ourUID == result[i].kp_eproc.e_pcred.p_ruid && 
				0 == strcasecmp( result[i].kp_proc.p_comm, FIREFLY_HELPER_PROC_N ) )
			{
				bRetVal = YES;
				break;
			}
		}
		free( result );
		
	}
	
	return bRetVal;	
}

// ------------------------------------------------------------------------
// launchHelperIfNeeded 
//
// Checks to see if our helper app is already running.  If not, launch
// it using NSTask (which doesn't have the issue in NSWorkspace where
// launching another app, even background-only, causes us to lose our
// window focus!).
// ------------------------------------------------------------------------
- (void)launchHelperIfNeeded
{
	if( ![self helperIsRunning] )
	{
		NSBundle *bundle = [NSBundle bundleWithPath:fireflyHelperPath];
		NSString *path = [bundle executablePath];
		NSTask *task = [[NSTask alloc] init];
		[task setLaunchPath:path];
		[task launch];
		[task release];
	}
}

// ------------------------------------------------------------------------
// makeProxyConnection
//
// Try to connect up our serverProxy object by looking it up by name.
// Returns a BOOL to indicate whether it has succeeded
// ------------------------------------------------------------------------
- (BOOL)makeProxyConnection
{
	BOOL bRetVal = NO;
	NSString *serviceName = [@"FireflyHelper" stringByAppendingString:(NSString*)userName];
	serverProxy = [NSConnection rootProxyForConnectionWithRegisteredName:serviceName 
																	host:nil];
	if( nil != serverProxy )
	{
		// This will notify us if the helper quits out from under us
		[[NSNotificationCenter defaultCenter] addObserver:self 
												 selector:@selector(connectionDied:) 
													 name:NSConnectionDidDieNotification 
												   object:nil];
		
		[serverProxy retain];
		[serverProxy setProtocolForProxy:@protocol(FireflyPrefsServerProtocol)];
		clientIdent = rand();
		[protocolChecker autorelease]; // in case we're being re-run
		protocolChecker = [[NSProtocolChecker 
			protocolCheckerWithTarget:self 
							 protocol:@protocol(FireflyPrefsClientProtocol)] retain];
		
		@try
		{
			bRetVal = [serverProxy registerClient:protocolChecker withIdentifier:clientIdent];
		}
		@catch( NSException *exception )
		{
			NSLog(@"makeProxyConnection caught %@: %@", 
				  [exception name], [exception  reason]);
		}

		// If we fail to register, we will ditch our server proxy and fail
		if( !bRetVal )
		{
			[serverProxy autorelease];
			serverProxy = nil;
		}
	}
	
	return bRetVal;
}


// ------------------------------------------------------------------------
// checkProxyConnection
//
// Checks to see if we have a valid proxy connection.  If we don't,
// this disables the controls in the panel, posts a dialog, and returns NO.
//
// Because of the dialog, this should only be called when a connection is
// believed to exist.
// ------------------------------------------------------------------------
- (BOOL)checkProxyConnection
{
	BOOL bRetVal = NO;
	if( nil != serverProxy )
	{
		@try
		{
			[serverProxy fireflyStatus];
			bRetVal = YES;
		}
		@catch( NSException *exception )
		{
			NSLog(@"checkProxyConnection caught %@: %@", 
				  [exception name], [exception  reason]);
			[serverProxy autorelease];
			serverProxy = nil;
			
			NSBeginCriticalAlertSheet( NSLocalizedString( @"Lost contact with Firefly Helper",
														  @"Alert message notifying the user of failure to get status" ),
									   @"OK", 
									   NULL, 
									   NULL, 
									   [[self mainView] window], 
									   nil, 
									   NULL, 
									   NULL, 
									   NULL, 
									   NSLocalizedString( @"Communication has been lost with the Firefly Helper. "
														  "Please close and re-open this Preference pane, and try again.",
														  @"Explanatory text for the connection-lost alert" ) );
			[self disableAllControls];
			[self updateServerStatus:kFireflyStatusInvalid];
		}
	}
	
	return bRetVal;
}

// ------------------------------------------------------------------------
// connectionDied
//
// This notification fires if an NSConnection dies.  We don't bother to
// save our server connection.  Rather, if this notification comes in, we
// check our connection.
// ------------------------------------------------------------------------
- (void)connectionDied:(NSNotification *)notification
{
	[self checkProxyConnection];
}


// ------------------------------------------------------------------------
// proxyTimerFired
//
// If the helper wasn't ready when we first checked, we try once a second
// for 10 seconds, using a timer
// ------------------------------------------------------------------------
- (void)proxyTimerFired:(NSTimer *) timer
{
#ifdef FIREFLY_DEBUG
	NSBeep();
#endif
	if( [self makeProxyConnection] )
	{
		[self updateServerStatus:[self fireflyStatus]];
		NSString *string = [self fireflyVersion];
		if( nil != string )
			[self versionChanged:string];
		string = [self fireflyConfigURL];
		if( nil != string )
			[self configUrlChanged:string];
		[ipcTimer invalidate];
		[ipcTimer autorelease];
		ipcTimer = nil;
	}
	else if( 10 < ++ipcTries )
	{
		[ipcTimer invalidate];
		[ipcTimer autorelease];
		ipcTimer = nil;
		[self updateServerStatus:kFireflyStatusInvalid];
		NSBeginCriticalAlertSheet( NSLocalizedString( @"Unable to get server status",
													  @"Alert message notifying the user of failure to get status" ),
								   @"OK", 
								   NULL, 
								   NULL, 
								   [[self mainView] window], 
								   nil, 
								   NULL, 
								   NULL, 
								   NULL, 
								   NSLocalizedString( @"An unexpected error occurred when trying to get the "
													  "status of the Firefly server. "
													  "Please close and re-open this Preference pane, and try again.",
													  @"Explanatory text for the failure-to-get-status alert" ) );
	}
	// else we try again when the timer fires
}

// ------------------------------------------------------------------------
// updateLogTextView
// ------------------------------------------------------------------------
- (void)updateLogTextView
{
	NSFileManager *mgr = [NSFileManager defaultManager];
	if( [mgr isReadableFileAtPath:logFilePath] )
	{
		NSDictionary *dict = [mgr fileAttributesAtPath:logFilePath traverseLink:YES];
		NSDate *modDate = [dict objectForKey:NSFileModificationDate];
		if( nil != modDate && ![logDate isEqualTo:modDate] )
		{
			// log date is the last time we processed an update
			[logDate autorelease];
			logDate = [modDate retain];
			NSString *newContents = nil;
			NSData *data = [NSData dataWithContentsOfFile:logFilePath];
			if( nil != data );
			{
				newContents = [[NSString alloc]
					initWithData:data encoding:NSUTF8StringEncoding];
			}
			if( nil == newContents || 0 == [newContents length] )
			{
				[logTextView setString:NSLocalizedString( @"The log file is empty.",
														  @"Text for empty log file" )];
			}
			else
			{
				// We're going to figure out our current scroll position and
				// the current selection (if any).  After we set the text,
				// we'll re-select the same range, and if we were at the bottom
				// of the scroller, we'll make sure we stay there.
				NSRange selection = [logTextView selectedRange];
				float scrollPos = 0.0;
				scrollPos = [[[logTextView enclosingScrollView] verticalScroller] floatValue];
				
				// Actually set the new text
				[logTextView setString:newContents];

				// Restore selection (it's lost when setString is called)
				[logTextView setSelectedRange:selection];
				
				// If we were previously scrolled to the end, scroll to the end.  
				// Otherwise, leave the window as it is. (This makes a range
				// of the very end of the view).
				if( 1.0 == scrollPos )
					[logTextView scrollRangeToVisible:NSMakeRange([[logTextView string] length], 0)];				
			}
			if( nil != newContents )
				[newContents autorelease];
		}
	}
	else
	{
		[logTextView setString:NSLocalizedString( @"The log file has not been created.",
												  @"Text for missing log file" )];
	}
}

// ------------------------------------------------------------------------
// logTimerFired
// ------------------------------------------------------------------------
- (void)logTimerFired:(NSTimer *) timer
{
	[self updateLogTextView];
}

// ========================================================================
// These functions wrap our IPC calls, so we can catch the exception that
// will be thrown if we try to access the server with the connection
// broken
// ========================================================================

// ------------------------------------------------------------------------
// startFirefly
// ------------------------------------------------------------------------
- (FireflyStartResult)startFirefly
{
	FireflyStartResult retVal = kFireflyStartFail;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy startFirefly];
		}
		@catch( NSException *exception )
		{
			NSLog(@"startFirefly caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// stopFirefly
// ------------------------------------------------------------------------
- (FireflyStopResult)stopFirefly
{
	FireflyStopResult retVal = kFireflyStopFail;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy stopFirefly];
		}
		@catch( NSException *exception )
		{
			NSLog(@"stopFirefly caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// restartFirefly
// ------------------------------------------------------------------------
- (FireflyRestartResult)restartFirefly;
{
	FireflyRestartResult retVal = kFireflyRestartFail;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy restartFirefly];
		}
		@catch( NSException *exception )
		{
			NSLog(@"restartFirefly caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// rescanLibrary
// ------------------------------------------------------------------------
- (FireflyRescanResult)rescanLibrary;
{
	FireflyRescanResult retVal = kFireflyRescanFail;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy rescanLibrary];
		}
		@catch( NSException *exception )
		{
			NSLog(@"rescanLibrary caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// fireflyStatus
// ------------------------------------------------------------------------
- (FireflyServerStatus)fireflyStatus;
{
	FireflyServerStatus retVal = kFireflyStatusInvalid;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy fireflyStatus];
		}
		@catch( NSException *exception )
		{
			NSLog(@"fireflyStatus caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// fireflyIsRunning
// ------------------------------------------------------------------------
- (BOOL)fireflyIsRunning;
{
	BOOL retVal = NO;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy fireflyIsRunning];
		}
		@catch( NSException *exception )
		{
			NSLog(@"fireflyStatus caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// fireflyVersion
// ------------------------------------------------------------------------
- (NSString*)fireflyVersion;
{
	NSString *retVal = nil;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy fireflyVersion];
		}
		@catch( NSException *exception )
		{
			NSLog(@"fireflyVersion caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// fireflyConfigURL
// ------------------------------------------------------------------------
- (NSString*)fireflyConfigURL;
{
	NSString *retVal = nil;
	if( nil != serverProxy )
	{
		@try
		{
			retVal = [serverProxy fireflyConfigURL];
		}
		@catch( NSException *exception )
		{
			NSLog(@"fireflyConfigURL caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
		}
	}
	return retVal;		
}

// ------------------------------------------------------------------------
// showHelperMenu
// ------------------------------------------------------------------------
- (void)showHelperMenu:(BOOL)bShowMenu
{
	if( nil != serverProxy )
	{
		@try
		{
			[serverProxy showHelperMenu:bShowMenu];
			bShowHelperMenu = bShowMenu;
			CFPreferencesSetAppValue( CFSTR(FF_PREFS_SHOW_MENU_EXTRA),
									  bShowMenu ? kCFBooleanTrue : kCFBooleanFalse,
									  CFSTR(FF_PREFS_DOMAIN) );
		}
		@catch( NSException *exception )
		{
			NSLog(@"fireflyConfigURL caught %@: %@", [exception name], [exception  reason]);
			[self checkProxyConnection];
			[helperMenuCheckbox setState:(bShowHelperMenu ? NSOnState : NSOffState)];
		}
	}
}


// ========================================================================
// Implementation of the FireflyPrefsClientProtocol
// ========================================================================

// ------------------------------------------------------------------------
// configUrlChanged
//
// We're being told that the server's configuration URL has changed
// ------------------------------------------------------------------------
- (void)configUrlChanged:(NSString *)newUrl
{
	if( 0 != [newUrl length] )
	{
		[webPageButton setEnabled:YES];
		[serverURL setString:newUrl];
		[webPageInfoText setStringValue:NSLocalizedString( @"Additional configuration options are "
														   "available from Firefly's built-in web page. "
														   "Click to open the page in your browser.",
														   @"Info text for the web page button when server "
														   "is running" )];
	}
}

// ------------------------------------------------------------------------
// versionChanged
//
// We're being told that the server's version has changed
// ------------------------------------------------------------------------
- (void)versionChanged:(NSString *)newVersion
{
	if( 0 != [newVersion length] )
		[serverVersionText setStringValue:newVersion];
	
}

// ------------------------------------------------------------------------
// newStatus
//
// We're being told that the server's status has changed
// ------------------------------------------------------------------------
- (void)statusChanged:(FireflyServerStatus)newStatus
{
	[self updateServerStatus:newStatus];
}

// ------------------------------------------------------------------------
// stillThere
//
// A "ping" to test the connection.  If we received it, we're here!
// ------------------------------------------------------------------------
- (BOOL)stillThere
{
	return YES;
}


@end
