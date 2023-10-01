<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="$t('page.settings.artwork.artwork')" />
      </template>
      <template #content>
        <div
          class="content"
          v-text="$t('page.settings.artwork.explanation-1')"
        />
        <div
          class="content"
          v-text="$t('page.settings.artwork.explanation-2')"
        />
        <settings-checkbox
          v-if="spotify.spotify_logged_in"
          category_name="artwork"
          option_name="use_artwork_source_spotify"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.spotify')" />
            <a href="https://www.spotify.com/" target="_blank">
              <span class="icon"><mdicon name="open-in-new" size="16" /></span>
            </a>
          </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="artwork"
          option_name="use_artwork_source_discogs"
        >
          <template #label>
            <span v-text="$t('page.settings.artwork.discogs')" />
            <a href="https://www.discogs.com/" target="_blank">
              <span class="icon"><mdicon name="open-in-new" size="16" /></span>
            </a>
          </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="artwork"
          option_name="use_artwork_source_coverartarchive"
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

export default {
  name: 'PageSettingsArtwork',
  components: { ContentWithHeading, SettingsCheckbox, TabsSettings },

  computed: {
    spotify() {
      return this.$store.state.spotify
    }
  }
}
</script>

<style></style>
