<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.services.spotify.title') }" />
    </template>
    <template #content>
      <div v-if="servicesStore.isSpotifyEnabled">
        <div
          v-text="
            $t('settings.services.authorise', {
              service: $t('settings.services.spotify.title')
            })
          "
        />
        <div
          class="notification help"
          v-text="
            $t('settings.services.spotify.info', {
              scopes: servicesStore.requiredSpotifyScopes.join(', ')
            })
          "
        />
        <div v-if="servicesStore.isSpotifyActive">
          <div
            v-text="
              $t('settings.services.authorised', {
                user: servicesStore.spotify.webapi_user
              })
            "
          />
          <div
            v-if="servicesStore.hasMissingSpotifyScopes"
            class="notification help is-danger is-light"
            v-text="
              $t('settings.services.spotify.reauthorize', {
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
              v-text="$t('actions.login')"
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
      <div
        v-else
        v-text="
          $t('settings.services.no-support', {
            service: $t('settings.services.spotify.title')
          })
        "
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.services.lastfm.title') }" />
    </template>
    <template #content>
      <div v-if="servicesStore.isLastfmEnabled">
        <div
          v-text="
            $t('settings.services.authorise', {
              service: $t('settings.services.lastfm.title')
            })
          "
        />
        <div
          class="notification help"
          v-text="$t('settings.services.lastfm.info')"
        />
        <div v-if="!servicesStore.isLastfmActive">
          <form @submit.prevent="loginLastfm">
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="lastfmCredentials.user"
                  class="input"
                  type="text"
                  :placeholder="$t('settings.services.username')"
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
                  :placeholder="$t('settings.services.password')"
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
      <div
        v-else
        v-text="
          $t('settings.services.no-support', {
            service: $t('settings.services.lastfm.title')
          })
        "
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('settings.services.listenbrainz.title') }"
      />
    </template>
    <template #content>
      <div
        v-text="
          $t('settings.services.authorise', {
            service: $t('settings.services.listenbrainz.title')
          })
        "
      />
      <div
        class="notification help"
        v-text="$t('settings.services.listenbrainz.info')"
      />
      <div v-if="!servicesStore.isListenBrainzActive">
        <div class="field is-grouped mt-5">
          <div class="control">
            <input
              v-model="listenbrainzToken"
              class="input"
              type="text"
              :placeholder="$t('settings.services.user-token')"
            />
          </div>
          <div class="control">
            <button
              class="button"
              @click="addListenBrainzToken"
              v-text="$t('actions.save')"
            />
          </div>
        </div>
        <div
          v-if="listenbrainzError"
          class="help is-danger"
          v-text="listenbrainzError"
        />
      </div>
      <div v-else>
        <div
          v-if="servicesStore.listenbrainz.user_name"
          v-text="
            $t('settings.services.authorised', {
              user: servicesStore.listenbrainz.user_name
            })
          "
        />
        <button
          class="button is-danger mt-5"
          @click="removeListenBrainzToken"
          v-text="$t('actions.logout')"
        />
      </div>
    </template>
  </content-with-heading>
</template>

<script setup>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { ref } from 'vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'
import { useServicesStore } from '@/stores/services'

const servicesStore = useServicesStore()
const { t } = useI18n()

const lastfmCredentials = ref({ password: '', user: '' })
const lastfmErrors = ref({ error: '', password: '', user: '' })
const listenbrainzToken = ref('')
const listenbrainzError = ref('')

const loginLastfm = async () => {
  const credentials = lastfmCredentials.value
  lastfmCredentials.value = {}
  const { errors } = await services.lastfm.login(credentials)
  lastfmErrors.value = errors
  servicesStore.initialise()
}

const logoutLastfm = () => {
  services.lastfm.logout()
  servicesStore.initialise()
}

const logoutSpotify = () => {
  services.spotify.logout()
  servicesStore.initialise()
}

const addListenBrainzToken = async () => {
  if (!listenbrainzToken.value.trim()) {
    listenbrainzError.value = t('settings.services.listenbrainz.token-required')
    return
  }
  const token = listenbrainzToken.value
  listenbrainzToken.value = ''
  await services.listenbrainz.addToken(token)
  servicesStore.initialise()
}

const removeListenBrainzToken = async () => {
  await services.listenbrainz.removeToken()
  servicesStore.initialise()
}
</script>
