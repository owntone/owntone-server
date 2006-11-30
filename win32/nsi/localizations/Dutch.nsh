!define LANG "DUTCH"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Bonjour voor Windows service niet gevonden. Installeer eerst Apple's Bonjour voor Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Service wordt gestopt..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Wacht tot de service gestopt is ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Bestemmingsfolder"
!insertmacro LANG_STRING STRING_DESTDETAIL "Setup zal ${PRODUCT_NAME} installeren in de volgende folder.$\r$\n$\r$\nOm in een andere folder te installeren, klik Doorzoek en selecteer een andere folder. Klik Volgende om door te gaan."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Kies Muziek Locatie"
!insertmacro LANG_STRING STRING_MUSICHEADER "Kies folder met music om te delen."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Muziek Folder"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Setup zal de muziek in de folder$\r$\n$\r$\nOm in een andere folder te delen, klik Doorzoek en selecteer een andere folder. Klik Installeer om de installatie te starten."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Website"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Deinstalleer"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Debug Modus"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Firefly Configuratie"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Advanceerde Configuratie"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) was succesvol verwijderd van uw computer."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Bent u zeker dat u $(^Name) wilt verwijderen en al zijn componenten?"
