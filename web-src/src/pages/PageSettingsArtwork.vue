<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.artwork.title') }" />
    </template>
    <template #content>
      <div class="content" v-text="$t('settings.artwork.explanation-1')" />
      <div class="content">
        <control-setting-switch
          :setting="settingsStore.get('artwork', 'streamurl_ignore')"
        />
        <control-setting-switch
          :setting="
            settingsStore.get('artwork', 'show_cover_artwork_in_album_lists')
          "
        />
      </div>
      <div class="content" v-text="$t('settings.artwork.explanation-2')" />
      <div class="content">
        <control-setting-switch
          v-if="servicesStore.isSpotifyActive"
          :setting="settingsStore.get('artwork', 'use_artwork_source_spotify')"
        />
        <control-setting-switch
          :setting="settingsStore.get('artwork', 'use_artwork_source_discogs')"
        />
        <control-setting-switch
          :setting="
            settingsStore.get('artwork', 'use_artwork_source_coverartarchive')
          "
        />
      </div>
    </template>
  </content-with-heading>
</template>

<script setup>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlSettingSwitch from '@/components/ControlSettingSwitch.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'

const servicesStore = useServicesStore()
const settingsStore = useSettingsStore()
</script>
