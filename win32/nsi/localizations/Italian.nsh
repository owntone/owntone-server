!define LANG "ITALIAN"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Il servizio Bonjour per Windows non è disponibile.  Installare Apple Bonjour per Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Arresto del servizio in corso..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Attesa per l'arresto del servizio ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Cartella di destinazione"
!insertmacro LANG_STRING STRING_DESTDETAIL "Setup installerà ${PRODUCT_NAME} nella seguente cartella.$\r$\n$\r$\nPer installare in una cartella differente, fare click su Sfoglia e selezionare un'altra cartella. Fare click su Prossimo per continuare."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Scelta della libreria musicale"
!insertmacro LANG_STRING STRING_MUSICHEADER "Scegliere la cartella contenente la musica da condividere."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Cartella con la musica"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Firefly condividerà la musica nella cartella seguente.$\r$\n$\r$\nPer condividere una cartella differente, fare click su Sfoglia e selezionare un'altra cartella. Fare click su Installa per iniziare."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Sito web"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Disinstalla"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Modalità Debug"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Configurazione di Firefly"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Configurazione Avanzata"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) è stato rimosso dal tuo computer con successo."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Sei sicuro di volere rimuovere completamente $(^Name) con tutti i relativi componenti?"

