!define LANG "FRENCH"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Le service Bonjour pour Windows n'a pas été trouvé. Veuillez installer le service Bonjour de Apple Pour Windows."
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "Arrêter le service..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "Attente de l'arrêt du service ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "Dossier de Destination"
!insertmacro LANG_STRING STRING_DESTDETAIL "Le programme d'installation va installer ${PRODUCT_NAME} dans le dossier suivant.$\r$\n$\r$\nPour l'installer dans un dossier différent, cliquez sur Parcourir et sélectionner un autre dossier. Cliquez sur Suivant pour continuer."

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "Choisissez l'emplacement de la Musique"
!insertmacro LANG_STRING STRING_MUSICHEADER "Choisissez le dossier contenant la musique à partager."
!insertmacro LANG_STRING STRING_MUSICFOLDER "Dossier de Musique"
!insertmacro LANG_STRING STRING_MUSICDETAIL "Le programme d'installation va partager la musique du dossier suivant.$\r$\n$\r$\nPour partager un dossier différent, cliquez sur Parcourir et sélectionnez un autre dossier. Cliquez sur Installer pour démarrer l'installation."

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "Site Web"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "Désinstaller"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "Mode Debug"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Configuration de Firefly"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "Configuration Avancée"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name) a été supprimé avec succès de votre ordinateur."
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "Êtes-vous sûr de vouloir supprimer définitivement $(^Name) et tous ses composants?"

