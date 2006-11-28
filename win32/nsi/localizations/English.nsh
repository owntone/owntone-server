!define LANG "ENGLISH"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Bonjour for Windows service not found.  Please install Apple's Bonjour for Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Stopping Service..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Waiting for service stop ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Destination Folder"
!insertmacro LANG_STRING STRING_DESTDETAIL "Setup will install ${PRODUCT_NAME} in the following folder.$\r$\n$\r$\nTo install in a different folder, click Browse and select another folder. Click Next to continue."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Choose Music Location"
!insertmacro LANG_STRING STRING_MUSICHEADER "Choose the folder containing music to share."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Music Folder"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Setup will share the music in the following folder.$\r$\n$\r$\nTo share a different folder, click Browse and select another folder. Click Install to start the installation."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Website"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Uninstall"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Debug Mode"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Firefly Configuration"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Advanced Configuration"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) was successfully removed from your computer."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Are you sure you want to completely remove $(^Name) and all of its components?"
