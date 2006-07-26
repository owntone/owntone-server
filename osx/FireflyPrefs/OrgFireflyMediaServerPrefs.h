/* OrgFireflyMediaServerPrefs */

#import <Cocoa/Cocoa.h>
#import <PreferencePanes/NSPreferencePane.h>
#import <CoreFoundation/CoreFoundation.h>
#import "../FireflyPrefsProtocol.h"

@interface OrgFireflyMediaServerPrefs : NSPreferencePane < FireflyPrefsClientProtocol >
{
    IBOutlet NSButton			*browseButton;
    IBOutlet NSTextField		*libraryField;
	IBOutlet NSImageView		*libraryIcon;
    IBOutlet NSTextField		*nameField;
    IBOutlet NSButton			*passwordCheckbox;
    IBOutlet NSButton			*helperMenuCheckbox;
    IBOutlet NSTextField		*passwordField;
    IBOutlet NSTextField		*portField;
	IBOutlet NSPopUpButton		*portPopup;
    IBOutlet NSPopUpButton		*serverStartOptions;
	IBOutlet NSTextField		*panelVersionText;
    IBOutlet NSTextField		*serverVersionText;
    IBOutlet NSButton			*startStopButton;
    IBOutlet NSTextField		*statusText;
    IBOutlet NSButton			*webPageButton;
	IBOutlet NSTextField		*webPageInfoText;
	IBOutlet NSTabView			*mainTabView;
	IBOutlet NSButton			*applyNowButton;
	IBOutlet NSProgressIndicator	*progressSpinner;
	IBOutlet NSTextView			*logTextView;
	IBOutlet NSScrollView		*logTextScroller;

			 CFStringRef		appID;
			 NSMutableString	*ourHostName;
			 NSMutableString	*fireflyFolderPath;
			 NSMutableString	*fireflyHelperPath;
			 NSMutableString	*serverURL;
			 NSMutableString	*logFilePath;
			 NSMutableString	*playlistPath;
			 NSString			*userName;
			 
	// Handling of the config file
			 NSMutableString	*configFilePath;
			 BOOL				configAppearsValid;
			 NSMutableString	*configError;
			 NSMutableArray		*configFileStrings;
			 unsigned long		idxOfServerName;
			 unsigned long		idxOfPassword;
			 unsigned long		idxOfPort;
			 unsigned long		idxOfLibraryPath;
			 unsigned long		idxOfNextSection;
			 unsigned long		idxOfDbPath;
			 unsigned long		idxOfLogPath;
			 unsigned long		idxOfPlaylistPath;
			 
	// Track whether we need to save
			 BOOL				bConfigNeedsSaving;

	// The actual preferences we manage with this GUI
			 NSMutableString	*serverName;
			 NSMutableString	*serverPassword;
			 NSMutableString	*libraryPath;
			 unsigned short		serverPort;  // 0 means automatic
			 BOOL				bStartServerOnLogin;
			 BOOL				bShowHelperMenu;
			 
	// Timer mechanism for setting up IPC
			 int					ipcTries;
			 NSTimer				*ipcTimer;
			 
	// Interprocess communication with Firefly Helper
			 id						serverProxy;
			 NSProtocolChecker		*protocolChecker;
			 int					clientIdent;

	// Log view updating
			 NSTimer				*logTimer;
			 NSDate					*logDate;
}

- (IBAction)browseButtonClicked:(id)sender;
- (IBAction)passwordChanged:(id)sender;
- (IBAction)shareNameChanged:(id)sender;
- (IBAction)portPopupChanged:(id)sender;
- (IBAction)portChanged:(id)sender;
- (IBAction)pwCheckBoxChanged:(id)sender;
- (IBAction)serverStartOptionChanged:(id)sender;
- (IBAction)startStopButtonClicked:(id)sender;
- (IBAction)webPageButtonClicked:(id)sender;
- (IBAction)applyNowButtonClicked:(id)sender;
- (IBAction)helperMenuCheckboxClicked:(id)sender;

// Overrides of NSPreferencePane methods
- (void)willSelect;
- (void)didSelect;
- (NSPreferencePaneUnselectReply)shouldUnselect;
- (void)willUnselect;

// Checking the validity of the Firefly installation.
- (BOOL)validateInstall;

// Tracking the need to save the config
- (void)setConfigNeedsSaving:(BOOL)needsSaving;

// UI utility functions
- (void)disableAllControls;
- (void)updateServerStatus:(FireflyServerStatus) status;
- (void)setIconForPath;

// Functions for loading and saving our configuration, as well as 
// reading and writing the config file.
- (BOOL)loadSettings;
- (BOOL)saveSettings;
- (BOOL)updateLoginItem;
- (void)readSettingsForHelper:(BOOL*)outHelper andServer:(BOOL*)outServer;
- (BOOL)readConfigFromPath:(NSString*)path;
- (BOOL)writeConfigToPath:(NSString*)path;
- (BOOL)createDefaultConfigFile;
- (NSString *)readValueFromBuf:(char*)buf startingAt:(int)idx unescapeCommas:(BOOL) bUnescapeCommas;
- (void)setDefaultValues;

// Finding or launching the helper
- (BOOL)helperIsRunning;
- (void)launchHelperIfNeeded;

// Validation of user entries
- (BOOL)control:(NSControl *)control isValidObject:(id) obj;
- (BOOL)currentTabIsValid;
- (void)alertForControl:(NSControl *)control;

// Alert delegate method(s)
- (void)alertDidEnd:(NSAlert *)alert returnCode:(int)returnCode	contextInfo:(void *)contextInfo;
- (void)applySheetDidEnd:(NSAlert *)alert returnCode:(int)returnCode contextInfo:(void *)contextInfo;

// Tab view delegate method(s)
- (BOOL)tabView:(NSTabView *)tabView shouldSelectTabViewItem:(NSTabViewItem *)tabViewItem;

// Browse panel delegate method(s)
- (void)browsePanelEnded:(NSOpenPanel *)panel returnCode:(int)panelResult contextInfo:(void *)contextInfo;

// Methods for dealing with the IPC proxy
- (BOOL)makeProxyConnection;
- (BOOL)checkProxyConnection;
- (void)proxyTimerFired:(NSTimer *) timer;
- (FireflyStartResult)startFirefly;
- (FireflyStopResult)stopFirefly;
- (FireflyRestartResult)restartFirefly;
- (FireflyRescanResult)rescanLibrary;
- (FireflyServerStatus)fireflyStatus;
- (BOOL)fireflyIsRunning;
- (NSString*)fireflyVersion;
- (NSString*)fireflyConfigURL;
- (void)showHelperMenu:(BOOL)bShowMenu;

// Log view stuff
- (void)updateLogTextView;
- (void)logTimerFired:(NSTimer *) timer;

@end
