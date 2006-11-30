!define LANG "GERMAN"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Der Bonjour-Dienst für Windows konnte nicht gefunden werden. Bitte installieren Sie Apples Bonjour für Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Beende Dienst..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Warte, bis Dienst beendet ist ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Zielpfad"
!insertmacro LANG_STRING STRING_DESTDETAIL "Setup wird ${PRODUCT_NAME} in folgenden Ordner installieren:$\r$\n$\r$\nUm in einen anderen Ordner als den ausgewählten zu installieren, klicken Sie Durchsuchen und wählen Sie einen anderen Ordner aus. Drücken Sie Weiter, um fortzufahren."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Wählen Sie den Pfad zu Ihrer Musiksammlung"
!insertmacro LANG_STRING STRING_MUSICHEADER "Wählen Sie den Pfad zu den Musikdateien, die Sie freigeben möchten."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Musik-Ordner"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Das Setup wird die Musik in folgenden Ordnern freigeben.$\r$\n$\r$\nUm in einen anderen Ordner als den ausgewählten freizugeben, klicken Sie Durchsuchen und wählen Sie einen anderen Ordner aus. Drücken Sie Installieren um den Installationsvorgang zu beginnen."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Website"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Deinstallieren"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Debug Modus"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Firefly Konfiguration"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Erweiterte Konfiguration"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) wurde erfolgreich von Ihrem Computer entfernt."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Sind Sie sicher, dass sie $(^Name) und damit alle Komponenten von Ihrem Computer entfernen wollen?"
