/*
 *  FireflyCommonConstants.h
 *
 *  Created by Mike Kobb on 7/12/06.
 *  Copyright 2006 Roku LLC. All rights reserved.
 *
 *  This file contains common constants and types needed by both the
 *  prefs pane and helper apps.
 */

#ifndef __ORG_FIREFLYMEDIASERVER_FIREFLY_COMMON_H
#define __ORG_FIREFLYMEDIASERVER_FIREFLY_COMMON_H

#include <sys/sysctl.h> // used by GetProcesses

#define FIREFLY_SERVER_NAME		"firefly"
#define FIREFLY_DIR_NAME		"Firefly"
#define FIREFLY_CONF_NAME		"firefly.conf"

#define FF_PREFS_DOMAIN				"org.fireflymediaserver.firefly"
#define FF_PREFS_LAUNCH_AT_LOGIN	"org.fireflymediaserver.launchAtLogin"
#define FF_PREFS_SHOW_MENU_EXTRA	"org.fireflymediaserver.showMenuExtra"

// Define this to enable certain debug output
//#define FIREFLY_DEBUG


typedef enum
{
	kFireflyStartInvalid = 0,
	kFireflyStartSuccess = 1,
	kFireflyStartFail    = 2
} FireflyStartResult;

typedef enum
{
	kFireflyStopInvalid = 0,
	kFireflyStopSuccess = 1,
	kFireflyStopFail    = 2
} FireflyStopResult;

typedef enum
{
	kFireflyRestartInvalid = 0,
	kFireflyRestartSuccess = 1,
	kFireflyRestartFail    = 2
} FireflyRestartResult;

typedef enum
{
	kFireflyRescanInvalid = 0,
	kFireflyRescanSuccess = 1,
	kFireflyRescanFail    = 2
} FireflyRescanResult;


typedef enum
{
	kFireflyStatusInvalid,
	kFireflyStatusStopped,
	kFireflyStatusStarting,
	kFireflyStatusActive,
	kFireflyStatusScanning,
	kFireflyStatusStopping,
	kFireflyStatusRestarting,
	kFireflyStatusStartFailed,
	kFireflyStatusCrashed
} FireflyServerStatus;

static NSString*
StringForFireflyStatus( FireflyServerStatus inStatus )
{
	NSString *retVal = nil;
	switch( inStatus )
	{
		case kFireflyStatusStopped:
			retVal = NSLocalizedString( @"Firefly is not running",
										@"Status message for Firefly" );
			break;
			
		case kFireflyStatusStarting:
			retVal = NSLocalizedString( @"Firefly is starting",
										@"Status message for Firefly" );
			break;

		case kFireflyStatusActive:
			retVal = NSLocalizedString( @"Firefly is running",
										@"Status message for Firefly" );
			break;

		case kFireflyStatusScanning:
			retVal = NSLocalizedString( @"Firefly is scanning the library",
										@"Status message for Firefly" );
			break;
		
		case kFireflyStatusStopping:
			retVal = NSLocalizedString( @"Firefly is stopping",
										@"Status message for Firefly" );
			break;
			
		case kFireflyStatusRestarting:
			retVal = NSLocalizedString( @"Firefly is restarting",
										@"Status message for Firefly" );
			break;

		case kFireflyStatusStartFailed:
			retVal = NSLocalizedString( @"Firefly failed to start",
										@"Status message for Firefly" );
			break;

		case kFireflyStatusCrashed:
			retVal = NSLocalizedString( @"Firefly stopped unexpectedly",
										@"Status message for Firefly" );
			break;

		case kFireflyStatusInvalid:
		default:
			retVal = NSLocalizedString( @"Firefly status is unknown",
										@"Status message for Firefly" );
			break;
	}
	
	return retVal;
}

// ===========================================================================
// Process management the Unix way -- Finding if the server is already 
// running, or finding a specific process
// ===========================================================================

// This just makes syntax more convenient (don't have to say 'struct' everyplace) 
typedef struct kinfo_proc kinfo_proc;

// ------------------------------------------------------------------------
// GetProcesses
//
// Static utility function allocates and returns an array of kinfo_proc
// structures representing the currently-running processes on the machine.
// The calling function is responsible for disposing the returned pointer
// with free()
//
// Because Firefly runs as a BSD daemon, the Process Manager is not useful
// in finding it.  Instead, we have to talk to the BSD layer.  This code
// was provided by Apple in a tech note.
// ------------------------------------------------------------------------
static void
GetProcesses( kinfo_proc **outResult, size_t *outLength)
{
	int                 err;
	kinfo_proc *        result;
	BOOL                done;
	static const int    name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	// Declaring name as const requires us to cast it when passing it to
	// sysctl because the prototype doesn't include the const modifier.
	// (That's the Apple comment, but they don't say *why* they made it const...)
	size_t              length;
	
	// We call sysctl with result == NULL and length == 0.
	// That will succeed, and set length to the appropriate length.
	// We then allocate a buffer of that size and call sysctl again
	// with that buffer.  If that succeeds, we're done.  If that fails
	// with ENOMEM, we have to throw away our buffer and loop.  Note
	// that the loop causes use to call sysctl with NULL again; this
	// is necessary because the ENOMEM failure case sets length to
	// the amount of data returned, not the amount of data that
	// could have been returned.
	
	result = NULL;
	done = NO;
	do 
	{
		// Call sysctl with a NULL buffer.
		length = 0;
		err = sysctl( (int *) name, (sizeof(name) / sizeof(*name)) - 1,
					  NULL, &length,
					  NULL, 0);
		if (err == -1)
			err = errno;
		
		// Allocate an appropriately sized buffer based on the results
		// from the previous call.
		if (err == 0)
		{
			result = malloc(length);
			if (result == NULL)
				err = ENOMEM;
		}
		
		// Call sysctl again with the new buffer.  If we get an ENOMEM
		// error, toss away our buffer and start again.
		if (err == 0)
		{
			err = sysctl( (int *) name, (sizeof(name) / sizeof(*name)) - 1,
						  result, &length,
						  NULL, 0);
			if (err == -1)
				err = errno;
			if (err == 0)
				done = YES;
			else if (err == ENOMEM)
			{
				free(result);
				result = NULL;
				err = 0;
			}
		}
	} while (err == 0 && !done);
	
	// Clean up and establish post conditions.
	if( err != 0 )
	{
		if( result != NULL)
			free(result);
		*outResult = NULL;
		*outLength = 0;
	}
	
	if( err == 0 )
	{
		*outResult = result;
		*outLength = length;
	}
}	



// __ORG_FIREFLYMEDIASERVER_FIREFLY_COMMON_H
#endif 

