!define LANG "JAPANESE"

!insertmacro LANG_STRING PRODUCT_NAME "Firefly Media Server"

; Install strings
!insertmacro LANG_STRING STRING_BONJOUR_ERROR "Windows版Bonjourサービスが見つかりません。Apple社のWindows版Bonjourをインストールしてください。"
!insertmacro LANG_STRING STRING_STOPPING_SERVICE "サービスを停止中..."
!insertmacro LANG_STRING STRING_WAITING_FOR_STOP "サービスの停止を待機中 ($9)"

; Strings for the application install directory panel
!insertmacro LANG_STRING STRING_DESTFOLDER "インストール先フォルダ"
!insertmacro LANG_STRING STRING_DESTDETAIL "セットアップは ${PRODUCT_NAME} を次のフォルダにインストールします。$\r$\n$\r$\n他のフォルダにインストールするには、「参照」ボタンをクリックして、他のフォルダを選択してください。続けるには、「次へ」ボタンをクリックしてください。"

; Strings for the music path directory panel
!insertmacro LANG_STRING STRING_MUSICTITLE "ミュージックの場所の選択"
!insertmacro LANG_STRING STRING_MUSICHEADER "共有するミュージックフォルダを選択してください。"
!insertmacro LANG_STRING STRING_MUSICFOLDER "ミュージックフォルダ"
!insertmacro LANG_STRING STRING_MUSICDETAIL "セットアップは次のフォルダにあるミュージックを共有します。$\r$\n$\r$\n他のフォルダを共有するには、「参照」ボタンをクリックして、他のフォルダを選択してください。インストールを開始するには、「インストール」ボタンをクリックしてください。"

; These are for the startmenu shortcuts
!insertmacro LANG_BOTHSTRING STRING_WEBSITE "ウェブサイト"
!insertmacro LANG_BOTHSTRING STRING_UNINSTALL "アンインストール"
!insertmacro LANG_BOTHSTRING STRING_DEBUG_MODE "デバッグモード"
!insertmacro LANG_BOTHSTRING STRING_FF_CONFIGURATION "Firefly設定"
!insertmacro LANG_BOTHSTRING STRING_ADV_CONFIG "詳細の設定"

; Uninstall Strings
!insertmacro LANG_UNSTRING STRING_UNINSTALLED "$(^Name)を正常にアンインストールすることができました。"
!insertmacro LANG_UNSTRING STRING_AREYOUSURE "本当に$(^Name)を完全にアンインストールしてもよろしいですか？"
