<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.general.language') }"
      />
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
      <heading-title
        :content="{ title: $t('page.settings.general.navigation-items') }"
      />
    </template>
    <template #content>
      <div
        class=" "
        v-text="$t('page.settings.general.navigation-item-selection')"
      />
      <div
        class="notification is-size-7"
        v-text="$t('page.settings.general.navigation-item-selection-info')"
      />
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_playlists"
      >
        <template #label>
          <span v-text="$t('page.settings.general.playlists')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_music"
      >
        <template #label>
          <span v-text="$t('page.settings.general.music')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_podcasts"
      >
        <template #label>
          <span v-text="$t('page.settings.general.podcasts')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_audiobooks"
      >
        <template #label>
          <span v-text="$t('page.settings.general.audiobooks')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_radio"
      >
        <template #label>
          <span v-text="$t('page.settings.general.radio')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_files"
      >
        <template #label>
          <span v-text="$t('page.settings.general.files')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_menu_item_search"
      >
        <template #label>
          <span v-text="$t('page.settings.general.search')" />
        </template>
      </control-setting-switch>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.general.now-playing-page') }"
      />
    </template>
    <template #content>
      <control-setting-switch
        category="webinterface"
        name="show_filepath_now_playing"
      >
        <template #label>
          <span v-text="$t('page.settings.general.show-path')" />
        </template>
      </control-setting-switch>
      <control-setting-switch
        category="webinterface"
        name="show_composer_now_playing"
      >
        <template #label>
          <span v-text="$t('page.settings.general.show-composer')" />
        </template>
        <template #help>
          <span v-text="$t('page.settings.general.show-composer-info')" />
        </template>
      </control-setting-switch>
      <control-setting-text-field
        category="webinterface"
        name="show_composer_for_genre"
        :disabled="!settingsStore.show_composer_now_playing"
        :placeholder="$t('page.settings.general.genres')"
      >
        <template #label>
          <span v-text="$t('page.settings.general.show-composer-genres')" />
        </template>
        <template #help>
          <i18n-t
            keypath="page.settings.general.show-composer-genres-help"
            tag="p"
            class="help"
            scope="global"
          >
            <br />
          </i18n-t>
        </template>
      </control-setting-text-field>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.general.recently-added-page') }"
      />
    </template>
    <template #content>
      <control-setting-integer-field
        category="webinterface"
        name="recently_added_limit"
      >
        <template #label>
          <span v-text="$t('page.settings.general.recently-added-page-info')" />
        </template>
      </control-setting-integer-field>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSettingIntegerField from '@/components/ControlSettingIntegerField.vue'
import ControlSettingSwitch from '@/components/ControlSettingSwitch.vue'
import ControlSettingTextField from '@/components/ControlSettingTextField.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    HeadingTitle,
    TabsSettings
  },
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  computed: {
    locale: {
      get() {
        const languages = this.$i18n.availableLocales
        let locale = languages.find((lang) => lang === this.$i18n.locale)
        const [partial] = this.$i18n.locale.split('-')
        if (!locale) {
          locale = languages.find((lang) => lang === partial)
        }
        if (!locale) {
          locale = languages.forEach((lang) => lang.split('-')[0] === partial)
        }
        if (!locale) {
          locale = this.$i18n.fallbackLocale
        }
        return locale
      },
      set(locale) {
        this.$i18n.locale = locale
      }
    }
  }
}
</script>
