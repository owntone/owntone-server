//
//  FireflyServer.m
//
//  The "model" part of our Model-View-Controller trio.  Represents the
//  firefly server itself, and encapsulates launching, quitting, status,
//  etc.
//
//  Created by Mike Kobb on 7/12/06.
//  Copyright 2006 Roku, LLC. All rights reserved.
//
#import <CoreFoundation/CoreFoundation.h>
#import <netinet/in.h>	// for sockaddr_in
#include <arpa/inet.h>	// for inet_ntoa and so forth
#include <sys/socket.h> // AF_INET6
#import "FireflyServer.h"

@implementation FireflyServer

// ---------------------------------------------------------------------------
// initWithServerPath
//
// Initialize the server object to manage the server at the given path.
// ---------------------------------------------------------------------------
- (id)initWithServerPath:(NSString *) path
{
	if( ( self = [super init] ) != nil )
	{
		fireflyServerPath = [[NSString stringWithString:path] retain];
		serverTask = nil;
		delegate = nil;
		serverVersion = nil;
		serverURL = nil;
		status = kFireflyStatusStopped;
		
		// Bonjour stuff below
		netBrowser = [[NSNetServiceBrowser alloc] init];
		pendingNetServices = [[NSMutableArray arrayWithCapacity:5] retain];
		fireflyService = nil;
		
		// Pick a random ffid that we'll be able to use to identify our
		// server easily
		srand((unsigned int)time(NULL));
		sprintf( ffid, "%08x", rand() );
		
		// Register for task ending notifications, so we can find out
		// if our server process quits.
		[[NSNotificationCenter defaultCenter] addObserver:self 
												 selector:@selector(taskEnded:) 
													 name:NSTaskDidTerminateNotification 
												   object:nil];
	}
	
	return self;
}

// ---------------------------------------------------------------------------
// dealloc
//
// 
// ---------------------------------------------------------------------------
- (void)dealloc
{
	// First, kill the server!
	
	
	[fireflyServerPath release];
	[serverTask release];
	[serverVersion release];
	[serverURL release];
	[netBrowser release];
	[pendingNetServices release];
	[fireflyService release];
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[super dealloc];
}

// ---------------------------------------------------------------------------
// setup
//
// Not to be confused with 'start', this function is to be called before 
// starting to use the object.  It handles starting our Bonjour stuff and 
// so on.
// ---------------------------------------------------------------------------
- (void)setup
{
	// It's time to start our scan, limiting to the local host.
	[netBrowser setDelegate:self];
	[netBrowser searchForServicesOfType:@"_http._tcp." inDomain:@"local."];
}
	
// ---------------------------------------------------------------------------
// shutdown
//
// Not to be confused with 'stop', this function is to be called when
// preparing to dispose the object.  It handles shutting down our Bonjour
// stuff and so on.  But, it does also stop the server if it's running.
// ---------------------------------------------------------------------------
- (void)shutdown
{
	// shut down the firefly server
	[self setStatus:kFireflyStatusStopping];
	[self stop];
	
	// Now shut down the Bonjour scan.
	[netBrowser stop];
	
	// FIXME: Is it safe to do this right after calling stop, or should we 
	// put these in didStop?
	NSEnumerator *enumerator = [pendingNetServices objectEnumerator];
	NSNetService *service;
	while( service = [enumerator nextObject] )
		[service stop];
	[pendingNetServices removeAllObjects];
}

// ---------------------------------------------------------------------------
// setDelegate
//
// We will message our delegate when important things happen, like the server
// starting or stopping, etc.
// ---------------------------------------------------------------------------
- (void)setDelegate:(id) delegateToSet
{
	delegate = delegateToSet;
}

// ---------------------------------------------------------------------------
// isRunning
//
// Is the server running?
// ---------------------------------------------------------------------------
- (BOOL)isRunning
{
	BOOL retVal = NO;
	if( nil != serverTask )
		retVal = [serverTask isRunning];
	return retVal;
}

// ---------------------------------------------------------------------------
// start
//
// Starts the server.  Note that this function may fail if the server is
// already running.
// ---------------------------------------------------------------------------
- (BOOL)start
{
	BOOL retVal = NO;
	
	FireflyServerStatus curStatus = [self status];
	if( curStatus == kFireflyStatusStopped || 
		curStatus == kFireflyStatusStartFailed || 
		curStatus == kFireflyStatusCrashed )
	{
		retVal = [self startAndUpdateStatus:YES];
	}
	
	return retVal;
}

// ---------------------------------------------------------------------------
// stop
//
// Signals the server to stop. Returns YES if the signal was sent successfully
// ---------------------------------------------------------------------------
- (BOOL)stop
{
	BOOL retVal = NO;
	if( nil != serverTask )
	{
		[self setStatus:kFireflyStatusStopping];
		[serverTask terminate];
		retVal = YES;
	}
	return retVal;
}

// ---------------------------------------------------------------------------
// restart
//
// restarts the server.  Tells the server to shut down after setting our
// status to "restarting".  When the server shuts down, the taskEnded
// method will see that status, and restart the server.
// ---------------------------------------------------------------------------
- (BOOL)restart
{
	BOOL retVal = NO;
	if( nil != serverTask )
	{
		[self setStatus:kFireflyStatusRestarting];
		[serverTask terminate];
		retVal = YES;
	}
	return retVal;
}

// ---------------------------------------------------------------------------
// status
//
// Returns the current server status
// ---------------------------------------------------------------------------
- (FireflyServerStatus)status
{
	return status;
}

// ---------------------------------------------------------------------------
// version
//
// Returns the current server version, or nil if it's not yet known
// ---------------------------------------------------------------------------
- (NSString *)version
{
	return serverVersion;
}

// ---------------------------------------------------------------------------
// configURL
//
// Returns the server's advanced user configuration URL, or nil if it's not 
// yet known
// ---------------------------------------------------------------------------
- (NSString *)configURL
{
	return serverURL;
}

// ===========================================================================
// Private utilities.
// ===========================================================================

// ---------------------------------------------------------------------------
// setStatus
//
// Sets the status and notifies interested parties of the change.
// ---------------------------------------------------------------------------
- (void)setStatus:(FireflyServerStatus) newStatus
{
	status = newStatus;
	
	[[NSNotificationCenter defaultCenter] postNotificationName:@STATUS_CHANGE
														object:self];
}

// ---------------------------------------------------------------------------
// setURL
//
// Sets the server config URL and notifies interested parties of the change.
// ---------------------------------------------------------------------------
- (void)setURL:(NSString *) newUrl
{
	[serverURL autorelease];
	serverURL = [[NSString stringWithString:newUrl] retain];
	[[NSNotificationCenter defaultCenter] postNotificationName:@URL_CHANGE
														object:self];
}

// ---------------------------------------------------------------------------
// setVersion
//
// Sets the server version and notifies interested parties of the change.
// ---------------------------------------------------------------------------
- (void)setVersion:(NSString *)newVersion
{
	[serverVersion autorelease];
	serverVersion = [[NSString stringWithString:newVersion] retain];
	[[NSNotificationCenter defaultCenter] postNotificationName:@VERSION_CHANGE
														object:self];
}

// ---------------------------------------------------------------------------
// taskEnded
//
// We register this function to be called when tasks end.  If the task is
// our server task, then we dispose the (now useless) object and notify
// interested parties of the change.  We check for normal versus abnormal
// termination and set status accordingly.
// ---------------------------------------------------------------------------
- (void)taskEnded:(NSNotification *)notification
{
	if( serverTask == [notification object] )
	{
		int termStatus = [[notification object] terminationStatus];
		[serverTask autorelease];
		serverTask = nil;
		if( kFireflyStatusRestarting == status )
		{
			// Don't post the message saying that the server stopped;
			// just start up and let the success or failure of that startup
			// handle the status update.
			[self startAndUpdateStatus:NO];
		}
		else
		{
			if( 0 == termStatus )
				[self setStatus:kFireflyStatusStopped];
			else if( kFireflyStatusStarting == status )
				[self setStatus:kFireflyStatusStartFailed];
			else
				[self setStatus:kFireflyStatusCrashed];
			NSLog(@"Server Task ended with status %d\n",  termStatus);
		}
	}
}

// ---------------------------------------------------------------------------
// fireflyConfigFilePath
//
// Build the path to the config file, test that it's valid and return it.  If
// we can't find a file at the expected location, we return nil
// ---------------------------------------------------------------------------
- (NSString*)fireflyConfigFilePath
{
	NSString *retVal = nil;
	NSArray * appSupportDirArray = 
		NSSearchPathForDirectoriesInDomains( NSLibraryDirectory,
											 NSUserDomainMask,
											 YES );
	if( [appSupportDirArray count] > 0 )
	{
		BOOL bIsDir = NO;
		NSFileManager *mgr = [NSFileManager defaultManager];
		NSString *configFilePath = 
			[[appSupportDirArray objectAtIndex:0] 
				stringByAppendingPathComponent:@"Application Support/Firefly/" 
				FIREFLY_CONF_NAME];
		if( [mgr fileExistsAtPath:configFilePath isDirectory:&bIsDir] && !bIsDir )
			retVal = configFilePath;
	}
	
	return retVal;
}	

// ---------------------------------------------------------------------------
// startAndUpdateStatus
//
// Private utility that actually starts the server.  If the bUpdate flag is
// NO, then this utility will leave the current status in place, even though
// the server is starting.  This is intended for use by the restart function,
// so that the status will remain in "restarting" until the server actually
// comes online (or fails)
// ---------------------------------------------------------------------------
- (BOOL)startAndUpdateStatus:(BOOL)bUpdate
{
	BOOL retVal = NO;
	
	[self killRunningFireflies];
	
	NSString *configFilePath = [self fireflyConfigFilePath];
	if( nil != configFilePath )
	{
		NSArray *array = [NSArray arrayWithObjects:
			@"-y", @"-f", @"-c", 
			configFilePath,
			@"-b",
			[NSString stringWithUTF8String:ffid],  // best 10.4<->10.3 compromise method...
			nil];
		@try
		{
			serverTask = [[[NSTask alloc] init] retain];
			[serverTask setLaunchPath:fireflyServerPath];
			[serverTask setCurrentDirectoryPath:[fireflyServerPath stringByDeletingLastPathComponent]];
			[serverTask setArguments:array];
			
			if( bUpdate )
				[self setStatus:kFireflyStatusStarting];
			[serverTask launch];
			retVal = YES;
		}
		@catch( NSException *exception )
		{
			if( [[exception name] isEqual:NSInvalidArgumentException] )
				;
			NSLog(@"FireflyServer: Caught %@: %@", [exception name], [exception  reason]);
			[self setStatus:kFireflyStatusStartFailed];
		}
	}
	else
	{
		NSLog(@"couldn't find config file at %@\n", configFilePath);
	}
	return retVal;
}

// ---------------------------------------------------------------------------
// killRunningFireflies
//
// This may seem like paranoia, but things really go badly if there is more
// than one copy of Firefly running (e.g. started from the command line, or
// failing to quit when signaled).  So, we enforce some preconditions here.
// ---------------------------------------------------------------------------
- (void)killRunningFireflies
{
	kinfo_proc *result;
	size_t length;
	GetProcesses( &result, &length );
	
	// Okay, now we have our list of processes.  Let's find OUR copy of
	// firefly.  Note that Firefly runs as multiple processes, so we look
	// through all processes, not stopping when we find one.  We *are*
	// careful to only kill processes owned by the current user!
	if( NULL != result )
	{
		int procCount = length / sizeof(kinfo_proc);
		int i = 0;
		uid_t ourUID = getuid();
		for( ; i < procCount; i++ )
		{
			if( ourUID == result[i].kp_eproc.e_pcred.p_ruid && 
				0 == strcasecmp( result[i].kp_proc.p_comm, FIREFLY_SERVER_NAME ) )
			{
				NSLog(@"Killing rogue firefly, pid %d\n", result[i].kp_proc.p_pid);
				kill( result[i].kp_proc.p_pid, SIGKILL );
			}
		}
		free( result );
	}
}

// ===========================================================================
// Below are the delegate methods for Bonjour discovery of the server.
// ===========================================================================

// ---------------------------------------------------------------------------
// netServiceBrowserWillSearch:
//
// Lets us know that the Bonjour search has started
// ---------------------------------------------------------------------------
- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser *)netServiceBrowser
{
#ifdef FIREFLY_DEBUG
	NSLog(@"NSNetServiceBrowser started\n");
#endif
	bScanIsActive = YES;
}

// ---------------------------------------------------------------------------
// netServiceBrowserDidStopSearch:
//
// Should only stop if we ask it to, as when we're quitting the app
// ---------------------------------------------------------------------------
- (void)netServiceBrowserDidStopSearch:(NSNetServiceBrowser *)netServiceBrowser
{
#ifdef FIREFLY_DEBUG
	NSLog(@"NSNetServiceBrowser stopped\n");
#endif
	bScanIsActive = NO;
}

// ---------------------------------------------------------------------------
// netServiceBrowser:didRemoveService:moreComing:
//
// Called when a Bonjour service goes away.
// ---------------------------------------------------------------------------
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		 didRemoveService:(NSNetService *)netService
			   moreComing:(BOOL)moreServicesComing
{
	// Is it our service?  If so, we need to switch the status text and change
	// the start/stop button, and also update the web page button and text.
	if( [netService isEqual:fireflyService] )
	{
		[fireflyService autorelease];
		fireflyService = nil;
		// FIXME: AND?  Theoretically, we should be notified that our NSTask
		// went away, so this notification isn't needed to detect that the
		// server stopped.  But, what if due to some error, the Bonjour
		// service croaked but left the server itself running?
	}
}

// ---------------------------------------------------------------------------
// netServiceBrowser:didRemoveDomain:moreComing:
//
// unless our local host goes away, we really don't care.
// ---------------------------------------------------------------------------
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		  didRemoveDomain:(NSString *)domainName
			   moreComing:(BOOL)moreDomainsComing
{
}

// ---------------------------------------------------------------------------
// netServiceBrowser:didNotSearch:
//
// Called if the search failed to start.  We need to alert the user.
// ---------------------------------------------------------------------------
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
			 didNotSearch:(NSDictionary *)errorInfo
{
	// FIXME: display error info?  Try again?  Quit?
}

// ---------------------------------------------------------------------------
// netServiceBrowser:didFindService:moreComing:
//
// A Bonjour service has been discovered.  It might be our server.  We need
// to ask the service to resolve, so we can see if it is.
// ---------------------------------------------------------------------------
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		   didFindService:(NSNetService *)netService
			   moreComing:(BOOL)moreServicesComing
{
	// We need to ask this object to resolve itself so we can figure out if it's the server
	// we want.  In case the user closes the panel, we need to have a list of all the ones
	// pending so we can stop them.
	[pendingNetServices addObject:netService];
	[netService setDelegate:self];
	[netService resolve];

	if( !moreServicesComing )
		bScanIsActive = NO;
}

// ---------------------------------------------------------------------------
// netServiceBrowser:didFindDomain:moreComing:
//
// Don't think we care about this one, but I'm pretty sure we're supposed to
// implement it (why?)
// ---------------------------------------------------------------------------
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
			didFindDomain:(NSString *)domainName
			   moreComing:(BOOL)moreDomainsComing
{
}

// ---------------------------------------------------------------------------
// netServiceDidResolveAddress:
//
// We asked a service to resolve, and it has.  Time to check to see if it's
// our server.
// ---------------------------------------------------------------------------
- (void)netServiceDidResolveAddress:(NSNetService *)service
{
	// Is it a firefly service, and if so, is it ours?
	
	// NOTE: EXTREMELY IMPORTANT!
	// protocolSpecificInformation is a deprecated API, and the Tiger-on approved way is TXTRecordData.
	// protocolSpecificInformation, though, is available back to 10.2.  These return the data in
	// DIFFERENT FORMATS.  TXTRecordData returns the data in the mDNS-standard format of a packed
	// array of Pascal strings, while protocolSpecificInformation returns the strings separated by 0x01
	// characters.  The TXTRecordContainsKey function and related utilities assume (at least on Tiger)
	// the packed-Pascal format, so we have to use strstr instead.  Happily, all we really care
	// about here is that the ffid tag exists, so we don't need to do much parsing.
	const char *version = NULL;
	const char* txtRecordBytes = NULL;
	NSData *data = nil;
	
	NSString *txtRecord = [service protocolSpecificInformation];
	if( nil != txtRecord )
		data = [txtRecord dataUsingEncoding:NSUTF8StringEncoding];
	txtRecordBytes = (const char*)[data bytes];
	if( NULL != txtRecordBytes && 
		NULL != ( version = strnstr( txtRecordBytes, "\001mtd-version=", [data length] ) ) )
	{
		// Okay, this is a firefly server.  Let's see if it's *our* server
		int i = 0;
		char buf[256];   // max allowed size, but we'll still be careful not to overrun.
		strncpy( buf, txtRecordBytes, 255 );
		buf[255] = '\0';
#ifdef FIREFLY_DEBUG
		NSLog( @"Text record is: %s\n", buf );
#endif
		const char *ffidptr = strnstr( txtRecordBytes, "\001ffid=", [data length] );
		if( NULL != ffidptr )
		{
			// This is a bit of a pain due to the stuff described in the big
			// comment above.

			// advance over the key
			ffidptr += 6;
			while( '\0' != ffidptr[i] && 
				   '\001' != ffidptr[i]  && 
				   ((ffidptr-txtRecordBytes)+i) < [data length] )
			{
#ifdef FIREFLY_DEBUG
				NSLog(@"Adding %c (%d)to ffidptr\n", ffidptr[i], ffidptr[i]);
#endif
				buf[i] = ffidptr[i++];
			}
			buf[i] = '\0';
			NSLog(@"Comparing buf %s against our ffid %s\n", buf, ffid);
			if( 0 == strcmp( buf, ffid ) )
			{
				// WOOT!  This is us.  Get the version and port
				i = 0;
				version += 13;
				while( '\0' != version[i] && 
					   '\001' != version[i]  && 
					   ((version-txtRecordBytes)+i) < [data length] )
					buf[i] = version[i++];
				buf[i] = '\0';
				[self setVersion:[NSString stringWithUTF8String:buf]];
				
				// Time to get the port.
				NSArray *svcAddresses = [service addresses];
				if( 0 != [svcAddresses count] )
				{
					NSData *addrData = [svcAddresses objectAtIndex:0];
					struct sockaddr_in *addrPtr = (struct sockaddr_in*)[addrData bytes];
					if( NULL != addrPtr )
					{
						serverPort = ntohs(addrPtr->sin_port);
						[self setURL:[NSString stringWithFormat:@"http://localhost:%u", serverPort]];
					}
				}
				
				// Okay, it's the one we want, so let's remember it and update
				// our status
				[fireflyService autorelease];
				fireflyService = [service retain];
				[self setStatus:kFireflyStatusActive];
#ifdef FIREFLY_DEBUG
				NSBeep();
				sleep(1);
				NSBeep();
				sleep(1);
				NSBeep();
#endif
			}
		}
	}	

	// It's no longer pending, so remove from array.  If it was ours, we've
	// retained it.
	[pendingNetServices removeObject:service];
	
	// If we're no longer scanning and we've exhausted all the
	// services that we found without identifying the correct server,
	// we need to ...?
	if( !bScanIsActive && 0 == [pendingNetServices count] && nil == fireflyService )
		; //FIXME
}

// ---------------------------------------------------------------------------
// netService:didNotResolve:
//
// We tried to resolve a service, and failed.  It's probably not really
// running.  We could always try again, but it doesn't seem to be necessary
// ---------------------------------------------------------------------------
- (void)netService:(NSNetService *)service didNotResolve:(NSDictionary *)errorInfo
{
	[pendingNetServices removeObject:service];
#ifdef FIREFLY_DEBUG
	if( nil == fireflyService )
		NSLog(@"Failed to resolve service: %@\n", [errorInfo valueForKey:NSNetServicesErrorCode] );
#endif
}

// ---------------------------------------------------------------------------
// netServiceWillResolve:
//
// Just lets us know resolution has started.
// ---------------------------------------------------------------------------
- (void)netServiceWillResolve:(NSNetService *)service
{
}

@end
