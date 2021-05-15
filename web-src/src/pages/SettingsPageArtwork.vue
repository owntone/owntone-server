<template>
  <div>
    <tabs-settings></tabs-settings>

    <content-with-heading>
      <template slot="heading-left">
        <div class="title is-4">Artwork</div>
      </template>

      <template slot="content">
        <div class="content">
          <p>
            OwnTone supports PNG and JPEG artwork which is either placed as separate image files in the library,
            embedded in the media files or made available online by radio stations.
          </p>
          <p>In addition to that, you can enable fetching artwork from the following artwork providers:</p>
        </div>
        <settings-checkbox category_name="artwork" option_name="use_artwork_source_spotify" v-if="spotify.libspotify_logged_in">
          <template slot="label"> Spotify</template>
        </settings-checkbox>
        <settings-checkbox category_name="artwork" option_name="use_artwork_source_discogs">
          <template slot="label"> Discogs (<a href="https://www.discogs.com/">https://www.discogs.com/</a>)</template>
        </settings-checkbox>
        <settings-checkbox category_name="artwork" option_name="use_artwork_source_coverartarchive">
          <template slot="label"> Cover Art Archive (<a href="https://coverartarchive.org/">https://coverartarchive.org/</a>)</template>
        </settings-checkbox>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsSettings from '@/components/TabsSettings'
import SettingsCheckbox from '@/components/SettingsCheckbox'

export default {
  name: 'SettingsPageArtwork',
  components: { ContentWithHeading, TabsSettings, SettingsCheckbox },

  computed: {
    spotify () {
      return this.$store.state.spotify
    }
  }
}
</script>

<style>
</style>
