<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="$t('page.settings.artwork.title')" />
      </template>
      <template #content>
        <div
          class="content"
          v-text="$t('page.settings.artwork.explanation-1')"
        />
        <settings-checkbox category="artwork" name="streamurl_ignore">
          <template #label>
            <span v-text="$t('page.settings.artwork.streaming')" />
          </template>
        </settings-checkbox>
        <settings-checkbox
          category="artwork"
          name="show_cover_artwork_in_album_lists"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.show-coverart')" />
          </template>
        </settings-checkbox>
        <div
          class="content"
          v-text="$t('page.settings.artwork.explanation-2')"
        />
        <settings-checkbox
          v-if="spotify.spotify_logged_in"
          category="artwork"
          name="use_artwork_source_spotify"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.spotify')" />
            <a href="https://www.spotify.com/" target="_blank">
              <span class="icon"><mdicon name="open-in-new" size="16" /></span>
            </a>
          </template>
        </settings-checkbox>
        <settings-checkbox category="artwork" name="use_artwork_source_discogs">
          <template #label>
            <span v-text="$t('page.settings.artwork.discogs')" />
            <a href="https://www.discogs.com/" target="_blank">
              <span class="icon"><mdicon name="open-in-new" size="16" /></span>
            </a>
          </template>
        </settings-checkbox>
        <settings-checkbox
          category="artwork"
          name="use_artwork_source_coverartarchive"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.coverartarchive')" />
            <a href="https://coverartarchive.org/" target="_blank">
              <span class="icon"><mdicon name="open-in-new" size="16" /></span>
            </a>
          </template>
        </settings-checkbox>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import SettingsCheckbox from '@/components/SettingsCheckbox.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'PageSettingsArtwork',
  components: { ContentWithHeading, SettingsCheckbox, TabsSettings },

  setup() {
    return { servicesStore: useServicesStore() }
  },

  computed: {
    spotify() {
      return this.servicesStore.spotify
    }
  }
}
</script>

<style></style>
