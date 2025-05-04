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
                  v-model="lastfmCredentials.user"
                  class="input"
                  type="text"
                  :placeholder="$t('page.settings.services.username')"
                />
                <div
                  v-if="lastfmErrors"
                  class="help is-danger"
                  v-text="lastfmErrors.user"
                />
              </div>
              <div class="control">
                <input
                  v-model="lastfmCredentials.password"
                  class="input"
                  type="password"
                  :placeholder="$t('page.settings.services.password')"
                />
                <div
                  v-if="lastfmErrors"
                  class="help is-danger"
                  v-text="lastfmErrors.password"
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
            <div
              v-if="lastfmErrors"
              class="help is-danger"
              v-text="lastfmErrors.error"
            />
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
import services from '@/api/services'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'PageSettingsOnlineServices',
  components: { ContentWithHeading, HeadingTitle, TabsSettings },
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      lastfmCredentials: { password: '', user: '' },
      lastfmErrors: { error: '', password: '', user: '' }
    }
  },
  methods: {
    loginLastfm() {
      services.loginLastfm(this.lastfmCredentials).then((data) => {
        this.lastfmErrors = data.errors
        this.lastfmCredentials.password = ''
        if (data.success) {
          this.lastfmCredentials.user = ''
        }
      })
    },
    logoutLastfm() {
      services.logoutLastfm()
    },
    logoutSpotify() {
      services.logoutSpotify()
    }
  }
}
</script>
