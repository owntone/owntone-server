tell application "System Events"
	if ("Firefly Helper" is in (name of every process)) then
		quit application "Firefly Helper"
	end if
end tell
