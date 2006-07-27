tell application "System Events"
	quit application "System Preferences"
	-- why do we do the following block this way?  Because if Firefly has never been installed and we try to just quit Firefly Helper, AppleScript will put up a dialog asking for the user to find it.  That's not helpful.  The delay is to give System Preferences time to quit.  Otherwise, if we quit the Helper and Firefly happened to be the open pane, the "lost connection" dialog will pop up.
	if ("Firefly Helper" is in (name of every process)) then
		delay 2
		quit application "Firefly Helper"
	end if
	if "Firefly Helper" is in (name of every login item) then
		delete (every login item whose name is "Firefly Helper")
	end if
end tell

tell application "Finder"
	try
		set uninstallFolder to (make new folder with properties {name:"firefly-uninstall"} at desktop)
		set libraryFolder to folder "Library" of home
		try
			move (file "Firefly.prefPane" of folder "PreferencePanes" of libraryFolder) to uninstallFolder
		end try
		try
			move (folder "Firefly" of folder "Application Support" of libraryFolder) to uninstallFolder
		end try
		move uninstallFolder to trash
		display dialog "Uninstall was successful.  Please double-click the Firefly.prefPane icon to install the new version." buttons {"OK"} default button {"OK"}
	on error
		display dialog "An error occurred while uninstalling the old version of Firefly.  Please follow the manual uninstall instructions in the Read Me First! file." buttons {"OK"} default button {"OK"}
	end try
end tell

