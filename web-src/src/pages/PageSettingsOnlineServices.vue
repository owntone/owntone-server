<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.services.spotify.title') }"
      />
    </template>
    <template #content>
      <div v-if="servicesStore.isSpotifyEnabled">
        <div v-text="$t('page.settings.services.spotify.grant-access')" />
        <div
          class="notification help"
          v-text="
            $t('page.settings.services.spotify.requirements', {
              scopes: servicesStore.requiredSpotifyScopes.join(', ')
            })
          "
        />
        <div v-if="servicesStore.isSpotifyActive">
          <div
            v-text="
              $t('page.settings.services.spotify.user', {
                user: servicesStore.spotify.webapi_user
              })
            "
          />
          <div
            v-if="servicesStore.hasMissingSpotifyScopes"
            class="notification help is-danger is-light"
            v-text="
              $t('page.settings.services.spotify.reauthorize', {
                scopes: servicesStore.missingSpotifyScopes.join(', ')
              })
            "
          />
        </div>
        <div class="field is-grouped mt-5">
          <div v-if="servicesStore.isAuthorizationRequired" class="control">
            <a
              class="button"
              :href="servicesStore.spotify.oauth_uri"
              v-text="$t('page.settings.services.spotify.authorize')"
            />
          </div>
          <div v-if="servicesStore.isSpotifyActive" class="control">
            <button
              class="button is-danger"
              @click="logoutSpotify"
              v-text="$t('actions.logout')"
            />
          </div>
        </div>
      </div>
      <div v-else v-text="$t('page.settings.services.spotify.no-support')" />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.services.lastfm.title') }"
      />
    </template>
    <template #content>
      <div v-if="servicesStore.isLastfmEnabled">
        <div v-text="$t('page.settings.services.lastfm.grant-access')" />
        <div
          class="notification help"
          v-text="$t('page.settings.services.lastfm.info')"
        />
        <div v-if="!servicesStore.isLastfmActive">
          <form @submit.prevent="loginLastfm">
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="lastfm_login.user"
                  class="input"
                  type="text"
                  :placeholder="$t('page.settings.services.username')"
                />
                <div class="help is-danger" v-text="lastfm_login.errors.user" />
              </div>
              <div class="control">
                <input
                  v-model="lastfm_login.password"
                  class="input"
                  type="password"
                  :placeholder="$t('page.settings.services.password')"
                />
                <div
                  class="help is-danger"
                  v-text="lastfm_login.errors.password"
                />
              </div>
              <div class="control">
                <button
                  class="button"
                  type="submit"
                  v-text="$t('actions.login')"
                />
              </div>
            </div>
            <div class="help is-danger" v-text="lastfm_login.errors.error" />
          </form>
        </div>
        <div v-else>
          <button
            class="button is-danger"
            @click="logoutLastfm"
            v-text="$t('actions.logout')"
          />
        </div>
      </div>
      <div v-else v-text="$t('page.settings.services.lastfm.no-support')" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'PageSettingsOnlineServices',
  components: { ContentWithHeading, HeadingTitle, TabsSettings },
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      lastfm_login: {
        errors: { error: '', password: '', user: '' },
        password: '',
        user: ''
      }
    }
  },
  methods: {
    loginLastfm() {
      webapi.lastfm_login(this.lastfm_login).then((data) => {
        this.lastfm_login.user = ''
        this.lastfm_login.password = ''
        this.lastfm_login.errors.user = ''
        this.lastfm_login.errors.password = ''
        this.lastfm_login.errors.error = ''
        if (!data.success) {
          this.lastfm_login.errors.user = data.errors.user
          this.lastfm_login.errors.password = data.errors.password
          this.lastfm_login.errors.error = data.errors.error
        }
      })
    },
    logoutLastfm() {
      webapi.lastfm_logout()
    },
    logoutSpotify() {
      webapi.spotify_logout()
    }
  }
}
</script>
