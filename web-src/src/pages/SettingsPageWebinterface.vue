<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Navbar items</div>
      </template>

      <template #content>
        <p class="content">Select the top navigation bar menu items</p>
        <div class="notification is-size-7">
          If you select more items than can be shown on your screen then the
          burger menu will disappear.
        </div>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_playlists"
        >
          <template #label> Playlists </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_music"
        >
          <template #label> Music </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_podcasts"
        >
          <template #label> Podcasts </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_audiobooks"
        >
          <template #label> Audiobooks </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_radio"
        >
          <template #label> Radio </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_files"
        >
          <template #label> Files </template>
        </settings-checkbox>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_menu_item_search"
        >
          <template #label> Search </template>
        </settings-checkbox>
      </template>
    </content-with-heading>

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Album lists</div>
      </template>

      <template #content>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_cover_artwork_in_album_lists"
        >
          <template #label> Show cover artwork in album list </template>
        </settings-checkbox>
      </template>
    </content-with-heading>

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Now playing page</div>
      </template>

      <template #content>
        <settings-checkbox
          category_name="webinterface"
          option_name="show_composer_now_playing"
        >
          <template #label> Show composer </template>
          <template #info>
            If enabled the composer of the current playing track is shown on the
            &quot;now playing page&quot;
          </template>
        </settings-checkbox>
        <settings-textfield
          category_name="webinterface"
          option_name="show_composer_for_genre"
          :disabled="!settings_option_show_composer_now_playing"
          placeholder="Genres"
        >
          <template #label> Show composer only for listed genres </template>
          <template #info>
            <p class="help">
              Comma separated list of genres the composer should be displayed on
              the &quot;now playing page&quot;.
            </p>
            <p class="help">Leave empty to always show the composer.</p>
            <p class="help">
              The genre tag of the current track is matched by checking, if one
              of the defined genres are included. For example setting to
              <code>classical, soundtrack</code> will show the composer for
              tracks with a genre tag of &quot;Contemporary Classical&quot;.<br />
            </p>
          </template>
        </settings-textfield>
      </template>
    </content-with-heading>

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Recently added page</div>
      </template>

      <template #content>
        <settings-intfield
          category_name="webinterface"
          option_name="recently_added_limit"
        >
          <template #label>
            Limit the number of albums shown on the "Recently Added" page
          </template>
        </settings-intfield>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import SettingsCheckbox from '@/components/SettingsCheckbox.vue'
import SettingsTextfield from '@/components/SettingsTextfield.vue'
import SettingsIntfield from '@/components/SettingsIntfield.vue'

export default {
  name: 'SettingsPageWebinterface',
  components: {
    ContentWithHeading,
    TabsSettings,
    SettingsCheckbox,
    SettingsTextfield,
    SettingsIntfield
  },

  computed: {
    settings_option_show_composer_now_playing() {
      return this.$store.getters.settings_option_show_composer_now_playing
    }
  }
}
</script>

<style></style>
