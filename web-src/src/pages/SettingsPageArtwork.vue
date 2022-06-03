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
              <mdicon class="icon" name="open-in-new" size="16" />
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
              <mdicon class="icon" name="open-in-new" size="16" />
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
              <mdicon class="icon" name="open-in-new" size="16" />
            </a>
          </template>
        </settings-checkbox>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import SettingsCheckbox from '@/components/SettingsCheckbox.vue'

export default {
  name: 'SettingsPageArtwork',
  components: { ContentWithHeading, TabsSettings, SettingsCheckbox },

  computed: {
    spotify() {
      return this.$store.state.spotify
    }
  }
}
</script>

<style></style>
