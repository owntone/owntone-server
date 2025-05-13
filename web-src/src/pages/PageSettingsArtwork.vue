<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('page.settings.artwork.title') }" />
    </template>
    <template #content>
      <div class="content" v-text="$t('page.settings.artwork.explanation-1')" />
      <div class="content">
        <control-setting-switch category="artwork" name="streamurl_ignore">
          <template #label>
            <span v-text="$t('page.settings.artwork.streaming')" />
          </template>
        </control-setting-switch>
        <control-setting-switch
          category="artwork"
          name="show_cover_artwork_in_album_lists"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.show-coverart')" />
          </template>
        </control-setting-switch>
      </div>
      <div class="content" v-text="$t('page.settings.artwork.explanation-2')" />
      <div class="content">
        <control-setting-switch
          v-if="servicesStore.spotify_logged_in"
          category="artwork"
          name="use_artwork_source_spotify"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.spotify')" />
            <a href="https://www.spotify.com/" target="_blank">
              <span class="icon">
                <mdicon name="open-in-new" size="16" />
              </span>
            </a>
          </template>
        </control-setting-switch>
        <control-setting-switch
          category="artwork"
          name="use_artwork_source_discogs"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.discogs')" />
            <a href="https://www.discogs.com/" target="_blank">
              <span class="icon">
                <mdicon name="open-in-new" size="16" />
              </span>
            </a>
          </template>
        </control-setting-switch>
        <control-setting-switch
          category="artwork"
          name="use_artwork_source_coverartarchive"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.coverartarchive')" />
            <a href="https://coverartarchive.org/" target="_blank">
              <span class="icon">
                <mdicon name="open-in-new" size="16" />
              </span>
            </a>
          </template>
        </control-setting-switch>
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
