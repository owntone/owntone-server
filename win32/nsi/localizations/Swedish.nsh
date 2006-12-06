!define LANG "SVENSKA"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Bonjour för Windows tjänsten kan inte hittas.  Var vänlig installera Apple's Bonjour för Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Stannar tjänsten..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Väntar på att tjänsten ska stanna ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Destination"
!insertmacro LANG_STRING STRING_DESTDETAIL "Installations programmet kommer installera ${PRODUCT_NAME} i följande mapp.$\r$\n$\r$\nFör att installera i en annan map, klicka Bläddra och välj en annan map. Klicka på Nästa för att fortsätta."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Välj musik mapp"
!insertmacro LANG_STRING STRING_MUSICHEADER "Välj mappen som innehåller musik att dela ut."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Musik Mapp"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Installationen kommer dela ut musiken i följande mapp.$\r$\n$\r$\nFör att dela ut en annan mapp, klicka Bläddra och välj en annan mapp. Klicka Installera för att starta installationen."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Websida"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Avinstallera"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Debug Läge"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Firefly Konfiguration"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Avancerad Konfiguration"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) är fullständigt avinstallerad från din dator."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Är du säker på att du vill avinstallera $(^Name) och alla dess komponenter?"
