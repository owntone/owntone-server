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
      <pane-title :content="{ title: $t('settings.appearance.title') }" />
    </template>
    <template #content>
      <control-dropdown
        v-model:value="appearance"
        :options="settingsStore.appearances"
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
        v-for="setting in settingsStore.settings('webinterface', 'show_menu')"
        :key="setting.name"
        :setting="setting"
      />
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
        :setting="
          settingsStore.get('webinterface', 'show_filepath_now_playing')
        "
      />
      <control-setting-switch
        :setting="
          settingsStore.get('webinterface', 'show_composer_now_playing')
        "
      />
      <control-setting-text-field
        :disabled="!settingsStore.showComposerNowPlaying"
        :placeholder="$t('settings.webinterface.genres')"
        :setting="settingsStore.get('webinterface', 'show_composer_for_genre')"
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
        :setting="settingsStore.get('webinterface', 'recently_added_limit')"
      />
    </template>
  </content-with-heading>
</template>

<script setup>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSettingIntegerField from '@/components/ControlSettingIntegerField.vue'
import ControlSettingSwitch from '@/components/ControlSettingSwitch.vue'
import ControlSettingTextField from '@/components/ControlSettingTextField.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { computed } from 'vue'
import { useSettingsStore } from '@/stores/settings'

const settingsStore = useSettingsStore()

const appearance = computed({
  get: () => settingsStore.currentAppearance(),
  set: (value) => settingsStore.setAppearance(value)
})
const locale = computed({
  get: () => settingsStore.currentLocale(),
  set: (value) => settingsStore.setLocale(value)
})
</script>
