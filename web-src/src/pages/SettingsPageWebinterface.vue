<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">Settings</div>
    </template>

    <template slot="heading-right">
    </template>

    <template slot="content">
      <div class="heading fd-has-margin-bottom">Now playing page</div>

      <div class="field">
        <label class="checkbox">
          <input type="checkbox" :checked="settings_option_show_composer_now_playing" @change="set_timer_show_composer_now_playing" ref="checkbox_show_composer">
          Show composer
          <i class="is-size-7"
              :class="{
                'has-text-info': statusUpdateShowComposerNowPlaying === 'success',
                'has-text-danger': statusUpdateShowComposerNowPlaying === 'error'
              }">{{ info_option_show_composer_now_playing }}</i>
        </label>
        <p class="help has-text-justified">
          If enabled the composer of the current playing track is shown on the &quot;now playing page&quot;
        </p>
      </div>
      <fieldset :disabled="!settings_option_show_composer_now_playing">
        <div class="field">
          <label class="label has-text-weight-normal">
            Show composer only for listed genres
            <i class="is-size-7"
                :class="{
                  'has-text-info': statusUpdateShowComposerForGenre === 'success',
                  'has-text-danger': statusUpdateShowComposerForGenre === 'error'
                }">{{ info_option_show_composer_for_genre }}</i>
          </label>
          <div class="control">
            <input class="input" type="text" placeholder="Genres"
                :value="settings_option_show_composer_for_genre"
                @input="set_timer_show_composer_for_genre"
                ref="field_composer_for_genre">
          </div>
          <p class="help">
            Comma separated list of genres the composer should be displayed on the &quot;now playing page&quot;.
          </p>
          <p class="help">
            Leave empty to always show the composer.
          </p>
          <p class="help">
            The genre tag of the current track is matched by checking, if one of the defined genres are included.
            For example setting to <code>classical, soundtrack</code> will show the composer for tracks with
            a genre tag of &quot;Contemporary Classical&quot;.<br>
          </p>
        </div>
      </fieldset>

    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'SettingsPageWebinterface',
  components: { ContentWithHeading },

  data () {
    return {
      timerDelay: 2000,
      timerIdShowComposerNowPlaying: -1,
      timerIdShowComposerForGenre: -1,

      // <empty>: default/no changes, 'success': update succesful, 'error': update failed
      statusUpdateShowComposerNowPlaying: '',
      statusUpdateShowComposerForGenre: ''
    }
  },

  computed: {
    settings_category_webinterface () {
      return this.$store.getters.settings_webinterface
    },
    settings_option_show_composer_now_playing () {
      return this.$store.getters.settings_option_show_composer_now_playing
    },
    settings_option_show_composer_for_genre () {
      return this.$store.getters.settings_option_show_composer_for_genre
    },
    info_option_show_composer_for_genre () {
      if (this.statusUpdateShowComposerForGenre === 'success') {
        return '(setting saved)'
      } else if (this.statusUpdateShowComposerForGenre === 'error') {
        return '(error saving setting)'
      }
      return ''
    },
    info_option_show_composer_now_playing () {
      if (this.statusUpdateShowComposerNowPlaying === 'success') {
        return '(setting saved)'
      } else if (this.statusUpdateShowComposerNowPlaying === 'error') {
        return '(error saving setting)'
      }
      return ''
    }
  },

  methods: {
    set_timer_show_composer_now_playing () {
      if (this.timerIdShowComposerNowPlaying > 0) {
        window.clearTimeout(this.timerIdShowComposerNowPlaying)
        this.timerIdShowComposerNowPlaying = -1
      }

      this.statusUpdateShowComposerNowPlaying = ''
      const newValue = this.$refs.checkbox_show_composer.checked
      if (newValue !== this.settings_option_show_composer_now_playing) {
        this.timerIdShowComposerNowPlaying = window.setTimeout(this.update_show_composer_now_playing, this.timerDelay)
      }
    },

    update_show_composer_now_playing () {
      this.timerIdShowComposerNowPlaying = -1

      const newValue = this.$refs.checkbox_show_composer.checked
      if (newValue === this.settings_option_show_composer_now_playing) {
        this.statusUpdateShowComposerNowPlaying = ''
        return
      }

      const option = {
        category: this.settings_category_webinterface.name,
        name: 'show_composer_now_playing',
        value: newValue
      }
      webapi.settings_update(this.settings_category_webinterface.name, option).then(() => {
        this.$store.commit(types.UPDATE_SETTINGS_OPTION, option)
        this.statusUpdateShowComposerNowPlaying = 'success'
      }).catch(() => {
        this.statusUpdateShowComposerNowPlaying = 'error'
        this.$refs.checkbox_show_composer.checked = this.settings_option_show_composer_now_playing
      }).finally(() => {
        this.timerIdShowComposerNowPlaying = window.setTimeout(this.clear_status_show_composer_now_playing, this.timerDelay)
      })
    },

    set_timer_show_composer_for_genre () {
      if (this.timerIdShowComposerForGenre > 0) {
        window.clearTimeout(this.timerIdShowComposerForGenre)
        this.timerIdShowComposerForGenre = -1
      }

      this.statusUpdateShowComposerForGenre = ''
      const newValue = this.$refs.field_composer_for_genre.value
      if (newValue !== this.settings_option_show_composer_for_genre) {
        this.timerIdShowComposerForGenre = window.setTimeout(this.update_show_composer_for_genre, this.timerDelay)
      }
    },

    update_show_composer_for_genre () {
      this.timerIdShowComposerForGenre = -1

      const newValue = this.$refs.field_composer_for_genre.value
      if (newValue === this.settings_option_show_composer_for_genre) {
        this.statusUpdateShowComposerForGenre = ''
        return
      }

      const option = {
        category: this.settings_category_webinterface.name,
        name: 'show_composer_for_genre',
        value: newValue
      }
      webapi.settings_update(this.settings_category_webinterface.name, option).then(() => {
        this.$store.commit(types.UPDATE_SETTINGS_OPTION, option)
        this.statusUpdateShowComposerForGenre = 'success'
      }).catch(() => {
        this.statusUpdateShowComposerForGenre = 'error'
        this.$refs.field_composer_for_genre.value = this.settings_option_show_composer_for_genre
      }).finally(() => {
        this.timerIdShowComposerForGenre = window.setTimeout(this.clear_status_show_composer_for_genre, this.timerDelay)
      })
    },

    clear_status_show_composer_for_genre () {
      this.statusUpdateShowComposerForGenre = ''
    },

    clear_status_show_composer_now_playing () {
      this.statusUpdateShowComposerNowPlaying = ''
    }
  },

  filters: {
  }
}
</script>

<style>
</style>
