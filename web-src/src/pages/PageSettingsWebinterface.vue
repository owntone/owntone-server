<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.webinterface.language') }" />
    </template>
    <template #content>
      <control-dropdown
        v-model:value="locale"
        :options="settingsStore.locales"
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('settings.webinterface.navigation-items') }"
      />
    </template>
    <template #content>
      <div v-text="$t('settings.webinterface.navigation-item-selection')" />
      <div
        class="notification is-size-7"
        v-text="$t('settings.webinterface.navigation-item-selection-info')"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_playlists"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_music"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_podcasts"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_audiobooks"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_radio"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_files"
      >
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_search"
      >
      </control-setting-switch>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('settings.webinterface.player-page') }"
      />
    </template>
    <template #content>
      <control-setting-switch
        category="webinterface"
        name="show_filepath_now_playing"
      />
      <control-setting-switch
        category="webinterface"
        name="show_composer_now_playing"
      />
      <control-setting-text-field
        category="webinterface"
        name="show_composer_for_genre"
        :disabled="!settingsStore.showComposerNowPlaying"
        :placeholder="$t('settings.webinterface.genres')"
      >
        <template #help>
          <i18n-t
            tag="p"
            class="help"
            keypath="settings.webinterface.show-composer-genres-help"
            scope="global"
          >
            <slot><br /></slot>
          </i18n-t>
        </template>
      </control-setting-text-field>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('settings.webinterface.recently-added-page') }"
      />
    </template>
    <template #content>
      <control-setting-integer-field
        category="webinterface"
        name="recently_added_limit"
      />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSettingIntegerField from '@/components/ControlSettingIntegerField.vue'
import ControlSettingSwitch from '@/components/ControlSettingSwitch.vue'
import ControlSettingTextField from '@/components/ControlSettingTextField.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'PageSettingsWebinterface',
  components: {
    ContentWithHeading,
    ControlDropdown,
    ControlSettingIntegerField,
    ControlSettingSwitch,
    ControlSettingTextField,
    PaneTitle,
    TabsSettings
  },
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  computed: {
    locale: {
      get() {
        return this.settingsStore.currentLocale()
      },
      set(locale) {
        this.settingsStore.setLocale(locale)
      }
    }
  }
}
</script>
