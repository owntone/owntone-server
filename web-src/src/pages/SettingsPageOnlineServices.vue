<template>
  <div class="fd-page-with-tabs">
    <tabs-settings></tabs-settings>

    <content-with-heading>
      <template v-slot:heading-left>
        <div class="title is-4">Spotify</div>
      </template>

      <template v-slot:content>
        <div class="notification is-size-7" v-if="!spotify.spotify_installed">
          <p>OwnTone was either built without support for Spotify or libspotify is not installed.</p>
        </div>
        <div v-if="spotify.spotify_installed">
          <div class="notification is-size-7">
            <b>You must have a Spotify premium account</b>. <span v-if="use_libspotity">If you normally log into Spotify with your Facebook account you must first go to Spotify's web site where you can get the Spotify username and password that matches your account.</span>
          </div>

          <div v-if="use_libspotity">
            <p class="content">
              <b>libspotify</b> - Login with your Spotify username and password
            </p>
            <p v-if="spotify.libspotify_logged_in" class="fd-has-margin-bottom">
              Logged in as <b><code>{{ spotify.libspotify_user }}</code></b>
            </p>
            <form v-if="spotify.spotify_installed && !spotify.libspotify_logged_in" @submit.prevent="login_libspotify">
              <div class="field is-grouped">
                <div class="control is-expanded">
                  <input class="input" type="text" placeholder="Username" v-model="libspotify.user">
                  <p class="help is-danger">{{ libspotify.errors.user }}</p>
                </div>
                <div class="control is-expanded">
                  <input class="input" type="password" placeholder="Password" v-model="libspotify.password">
                  <p class="help is-danger">{{ libspotify.errors.password }}</p>
                </div>
                <div class="control">
                  <button class="button is-info">Login</button>
                </div>
              </div>
            </form>
            <p class="help is-danger">{{ libspotify.errors.error }}</p>
            <p class="help">
              libspotify enables OwnTone to play Spotify tracks.
            </p>
            <p class="help">
              OwnTone will not store your password, but will still be able to log you in automatically afterwards, because libspotify saves a login token.
            </p>
          </div>

          <div class="fd-has-margin-top">
            <p class="content">
              <b>Spotify Web API</b> - Grant access to the Spotify Web API
            </p>
            <p v-if="spotify.webapi_token_valid">
              Access granted for <b><code>{{ spotify.webapi_user }}</code></b>
            </p>
            <p class="help is-danger" v-if="spotify_missing_scope.length > 0">
              Please reauthorize Web API access to grant OwnTone the following additional access rights:
              <b><code>{{ spotify_missing_scope.join() }}</code></b>
            </p>
            <div class="field fd-has-margin-top ">
              <div class="control">
                <a class="button" :class="{ 'is-info': !spotify.webapi_token_valid || spotify_missing_scope.length > 0 }" :href="spotify.oauth_uri">Authorize Web API access</a>
              </div>
            </div>
            <p class="help">
              Access to the Spotify Web API enables scanning of your Spotify library. Required scopes are
              <code>{{ spotify_required_scope.join() }}</code>.
            </p>
            <div v-if="spotify.webapi_token_valid" class="field fd-has-margin-top ">
              <div class="control">
                <a class="button is-danger" @click="logout_spotify">Logout</a>
              </div>
            </div>
          </div>
        </div>
      </template>
    </content-with-heading>

    <content-with-heading>
      <template v-slot:heading-left>
        <div class="title is-4">Last.fm</div>
      </template>

      <template v-slot:content>
        <div class="notification is-size-7" v-if="!lastfm.enabled">
          <p>OwnTone was built without support for Last.fm.</p>
        </div>
        <div v-if="lastfm.enabled">
          <p class="content">
            <b>Last.fm</b> - Login with your Last.fm username and password to enable scrobbling
          </p>
          <div v-if="lastfm.scrobbling_enabled">
            <a class="button" @click="logoutLastfm">Stop scrobbling</a>
          </div>
          <div v-if="!lastfm.scrobbling_enabled">
            <form @submit.prevent="login_lastfm">
              <div class="field is-grouped">
                <div class="control is-expanded">
                  <input class="input" type="text" placeholder="Username" v-model="lastfm_login.user">
                  <p class="help is-danger">{{ lastfm_login.errors.user }}</p>
                </div>
                <div class="control is-expanded">
                  <input class="input" type="password" placeholder="Password" v-model="lastfm_login.password">
                  <p class="help is-danger">{{ lastfm_login.errors.password }}</p>
                </div>
                <div class="control">
                  <button class="button is-info" type="submit">Login</button>
                </div>
              </div>
              <p class="help is-danger">{{ lastfm_login.errors.error }}</p>
              <p class="help">
                OwnTone will not store your Last.fm username/password, only the session key. The session key does not expire.
              </p>
            </form>
          </div>
        </div>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import webapi from '@/webapi'

export default {
  name: 'SettingsPageOnlineServices',
  components: { ContentWithHeading, TabsSettings },

  data () {
    return {
      libspotify: { user: '', password: '', errors: { user: '', password: '', error: '' } },
      lastfm_login: { user: '', password: '', errors: { user: '', password: '', error: '' } }
    }
  },

  computed: {
    lastfm () {
      return this.$store.state.lastfm
    },

    spotify () {
      return this.$store.state.spotify
    },

    spotify_required_scope () {
      if (this.spotify.webapi_required_scope) {
        return this.spotify.webapi_required_scope.split(' ')
      }
      return []
    },

    spotify_missing_scope () {
      if (this.spotify.webapi_token_valid && this.spotify.webapi_granted_scope && this.spotify.webapi_required_scope) {
        return this.spotify.webapi_required_scope.split(' ').filter(scope => this.spotify.webapi_granted_scope.indexOf(scope) < 0)
      }
      return []
    },

    use_libspotify () {
      return this.$store.state.config.use_libspotify
    }
  },

  methods: {
    login_libspotify () {
      webapi.spotify_login(this.libspotify).then(response => {
        this.libspotify.user = ''
        this.libspotify.password = ''
        this.libspotify.errors.user = ''
        this.libspotify.errors.password = ''
        this.libspotify.errors.error = ''

        if (!response.data.success) {
          this.libspotify.errors.user = response.data.errors.user
          this.libspotify.errors.password = response.data.errors.password
          this.libspotify.errors.error = response.data.errors.error
        }
      })
    },

    logout_spotify () {
      webapi.spotify_logout()
    },

    login_lastfm () {
      webapi.lastfm_login(this.lastfm_login).then(response => {
        this.lastfm_login.user = ''
        this.lastfm_login.password = ''
        this.lastfm_login.errors.user = ''
        this.lastfm_login.errors.password = ''
        this.lastfm_login.errors.error = ''

        if (!response.data.success) {
          this.lastfm_login.errors.user = response.data.errors.user
          this.lastfm_login.errors.password = response.data.errors.password
          this.lastfm_login.errors.error = response.data.errors.error
        }
      })
    },

    logoutLastfm () {
      webapi.lastfm_logout()
    }
  },

  filters: {
    join (array) {
      return array.join(', ')
    }
  }
}
</script>

<style>
</style>
