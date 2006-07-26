//
//  FireflyServer.h
//  
//  The "model" part of our Model-View-Controller trio.  Represents the
//  firefly server itself, and encapsulates launching, quitting, status,
//  etc.
//
//  Created by Mike Kobb on 7/12/06.
//  Copyright 2006 Roku, LLC. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#include "../FireflyCommon.h"

#define	STATUS_CHANGE	"org.fireflymediaserver.status-change"
#define VERSION_CHANGE	"org.fireflymediaserver.version-change"
#define URL_CHANGE		"org.fireflymediaserver.url-change"

@interface FireflyServer : NSObject
{
			 NSString			*fireflyServerPath;
			 NSTask				*serverTask;
			 
			 NSString			*serverVersion;
			 NSString			*serverURL;
			 unsigned short		serverPort;
			 
			 id					delegate;
			 FireflyServerStatus	status;
			 
	// We use Bonjour to find our server port and help notice if the server
	// stops
			 NSNetServiceBrowser	*netBrowser;
			 BOOL					bScanIsActive;
			 NSMutableArray			*pendingNetServices;
			 NSNetService			*fireflyService;
			 char					ffid[9];
			 
}

// public methods for managing the lifecycle of the server object
- (void)setup;
- (void)shutdown;

// public methods for controlling the server process
- (id)initWithServerPath:(NSString *) serverPath;
- (BOOL)start;
- (BOOL)stop;
- (BOOL)restart;
- (void)setDelegate:(id) delegate;

// public methods for querying server status & properties
- (BOOL)isRunning;
- (FireflyServerStatus)status;
- (NSString *)version;
- (NSString *)configURL;

// private utilities
- (void)setStatus:(FireflyServerStatus)newStatus;
- (void)setURL:(NSString *)newURL;
- (void)setVersion:(NSString *)newVersion;
- (void)taskEnded:(NSNotification *)notification;
- (NSString*)fireflyConfigFilePath;
- (BOOL)startAndUpdateStatus:(BOOL)bUpdate;
- (void)killRunningFireflies;

// Bonjour delegate methods (NSNetServiceBrowser)
- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser *)netServiceBrowser;
- (void)netServiceBrowserDidStopSearch:(NSNetServiceBrowser *)netServiceBrowser;
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		 didRemoveService:(NSNetService *)netService
			   moreComing:(BOOL)moreServicesComing;
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		  didRemoveDomain:(NSString *)domainName
			   moreComing:(BOOL)moreDomainsComing;
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
			 didNotSearch:(NSDictionary *)errorInfo;
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
		   didFindService:(NSNetService *)netService
			   moreComing:(BOOL)moreServicesComing;
- (void)netServiceBrowser:(NSNetServiceBrowser *)netServiceBrowser 
			didFindDomain:(NSString *)domainName
			   moreComing:(BOOL)moreDomainsComing;

// Bonjour delegate methods (NSNetService)
- (void)netServiceDidResolveAddress:(NSNetService *)service;
- (void)netService:(NSNetService *)service didNotResolve:(NSDictionary *)errorInfo;
- (void)netServiceWillResolve:(NSNetService *)service;


@end
