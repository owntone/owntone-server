<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />
    <content-with-heading>
      <template #heading-left>
        <div
          class="title is-4"
          v-text="$t('page.settings.services.spotify.title')"
        />
      </template>
      <template #content>
        <div v-if="!spotify.spotify_installed" class="notification is-size-7">
          <p v-text="$t('page.settings.services.spotify.no-support')" />
        </div>
        <div v-if="spotify.spotify_installed">
          <div class="notification is-size-7">
            <span v-text="$t('page.settings.services.spotify.requirements')" />
          </div>
          <div class="mt-5">
            <p
              class="content"
              v-text="$t('page.settings.services.spotify.grant-access')"
            />
            <p v-if="spotify.webapi_token_valid">
              <span v-text="$t('page.settings.services.spotify.user')" />
              <code v-text="spotify.webapi_user" />
            </p>
            <p v-if="spotify_missing_scope.length > 0" class="help is-danger">
              <span v-text="$t('page.settings.services.spotify.reauthorize')" />
              <code v-text="spotify_missing_scope.join()" />
            </p>
            <div class="field mt-5">
              <div class="control">
                <a
                  class="button"
                  :class="{
                    'is-info':
                      !spotify.webapi_token_valid ||
                      spotify_missing_scope.length > 0
                  }"
                  :href="spotify.oauth_uri"
                  v-text="$t('page.settings.services.spotify.authorize')"
                />
              </div>
            </div>
            <p class="help">
              <span v-text="$t('page.settings.services.spotify.scopes')" />
              <code v-text="spotify_required_scope.join(', ')" />
            </p>
            <div v-if="spotify.webapi_token_valid" class="field mt-5">
              <div class="control">
                <a
                  class="button is-danger"
                  @click="logout_spotify"
                  v-text="$t('page.settings.services.logout')"
                />
              </div>
            </div>
          </div>
        </div>
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <div
          class="title is-4"
          v-text="$t('page.settings.services.lastfm.title')"
        />
      </template>
      <template #content>
        <div v-if="!lastfm.enabled" class="notification is-size-7">
          <p v-text="$t('page.settings.services.lastfm.no-support')" />
        </div>
        <div v-if="lastfm.enabled">
          <p
            class="content"
            v-text="$t('page.settings.services.lastfm.grant-access')"
          />
          <div v-if="lastfm.scrobbling_enabled">
            <a
              class="button"
              @click="logoutLastfm"
              v-text="$t('page.settings.services.lastfm.stop-scrobbling')"
            />
          </div>
          <div v-if="!lastfm.scrobbling_enabled">
            <form @submit.prevent="login_lastfm">
              <div class="field is-grouped">
                <div class="control is-expanded">
                  <input
                    v-model="lastfm_login.user"
                    class="input"
                    type="text"
                    :placeholder="$t('page.settings.services.username')"
                  />
                  <p class="help is-danger" v-text="lastfm_login.errors.user" />
                </div>
                <div class="control is-expanded">
                  <input
                    v-model="lastfm_login.password"
                    class="input"
                    type="password"
                    :placeholder="$t('page.settings.services.password')"
                  />
                  <p
                    class="help is-danger"
                    v-text="lastfm_login.errors.password"
                  />
                </div>
                <div class="control">
                  <button
                    class="button is-info"
                    type="submit"
                    v-text="$t('page.settings.services.login')"
                  />
                </div>
              </div>
              <p class="help is-danger" v-text="lastfm_login.errors.error" />
              <p
                class="help"
                v-text="$t('page.settings.services.lastfm.info')"
              />
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
  name: 'PageSettingsOnlineServices',
  components: { ContentWithHeading, TabsSettings },

  data() {
    return {
      lastfm_login: {
        user: '',
        password: '',
        errors: { user: '', password: '', error: '' }
      }
    }
  },

  computed: {
    lastfm() {
      return this.$store.state.lastfm
    },
    spotify() {
      return this.$store.state.spotify
    },
    spotify_missing_scope() {
      if (
        this.spotify.webapi_token_valid &&
        this.spotify.webapi_granted_scope &&
        this.spotify.webapi_required_scope
      ) {
        return this.spotify.webapi_required_scope
          .split(' ')
          .filter(
            (scope) => this.spotify.webapi_granted_scope.indexOf(scope) < 0
          )
      }
      return []
    },
    spotify_required_scope() {
      if (this.spotify.webapi_required_scope) {
        return this.spotify.webapi_required_scope.split(' ')
      }
      return []
    }
  },

  methods: {
    logout_spotify() {
      webapi.spotify_logout()
    },
    login_lastfm() {
      webapi.lastfm_login(this.lastfm_login).then((response) => {
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
    logoutLastfm() {
      webapi.lastfm_logout()
    }
  }
}
</script>

<style></style>
