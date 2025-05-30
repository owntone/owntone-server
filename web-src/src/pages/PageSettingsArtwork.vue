<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.artwork.title') }" />
    </template>
    <template #content>
      <div class="content" v-text="$t('settings.artwork.explanation-1')" />
      <div class="content">
        <control-setting-switch category="artwork" name="streamurl_ignore" />
        <control-setting-switch
          category="artwork"
          name="show_cover_artwork_in_album_lists"
        />
      </div>
      <div class="content" v-text="$t('settings.artwork.explanation-2')" />
      <div class="content">
        <control-setting-switch
          v-if="servicesStore.isSpotifyActive"
          category="artwork"
          name="use_artwork_source_spotify"
        />
        <control-setting-switch
          category="artwork"
          name="use_artwork_source_discogs"
        />
        <control-setting-switch
          category="artwork"
          name="use_artwork_source_coverartarchive"
        />
      </div>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlSettingSwitch from '@/components/ControlSettingSwitch.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'PageSettingsArtwork',
  components: {
    ContentWithHeading,
    ControlSettingSwitch,
    PaneTitle,
    TabsSettings
  },
  setup() {
    return { servicesStore: useServicesStore() }
  }
}
</script>
