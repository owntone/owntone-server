<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="$t('page.settings.devices.pairing')" />
      </template>
      <template #content>
        <!-- Paring request active -->
        <div v-if="pairing.active" class="notification">
          <form @submit.prevent="kickoff_pairing">
            <label class="label has-text-weight-normal">
              <span v-text="$t('page.settings.devices.pairing-request')" />
              <b v-text="pairing.remote" />
            </label>
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="pairing_req.pin"
                  class="input"
                  type="text"
                  :placeholder="$t('page.settings.devices.pairing-code')"
                />
              </div>
              <div class="control">
                <button
                  class="button is-info"
                  type="submit"
                  v-text="$t('page.settings.devices.send')"
                />
              </div>
            </div>
          </form>
        </div>
        <!-- No pairing requests -->
        <div v-if="!pairing.active" class="content">
          <p v-text="$t('page.settings.devices.no-active-pairing')" />
        </div>
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <div
          class="title is-4"
          v-text="$t('page.settings.devices.speaker-pairing')"
        />
      </template>
      <template #content>
        <p
          class="content"
          v-text="$t('page.settings.devices.speaker-pairing-info')"
        />
        <div v-for="output in outputs" :key="output.id">
          <div class="field">
            <div class="control">
              <input
                :id="output.id"
                v-model="output.selected"
                type="checkbox"
                class="switch is-rounded mr-2"
                @change="output_toggle(output.id)"
              />
              <label :for="output.id" class="checkbox" v-text="output.name" />
            </div>
          </div>
          <form
            v-if="output.needs_auth_key"
            class="mb-5"
            @submit.prevent="kickoff_verification(output.id)"
          >
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="verification_req.pin"
                  class="input"
                  type="text"
                  :placeholder="$t('page.settings.devices.verification-code')"
                />
              </div>
              <div class="control">
                <button
                  class="button is-info"
                  type="submit"
                  v-text="$t('page.settings.devices.verify')"
                />
              </div>
            </div>
          </form>
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
  name: 'PageSettingsRemotesOutputs',
  components: { ContentWithHeading, TabsSettings },

  data() {
    return {
      pairing_req: { pin: '' },
      verification_req: { pin: '' }
    }
  },

  computed: {
    outputs() {
      return this.$store.state.outputs
    },
    pairing() {
      return this.$store.state.pairing
    }
  },

  methods: {
    kickoff_pairing() {
      webapi.pairing_kickoff(this.pairing_req)
    },
    kickoff_verification(outputId) {
      webapi.output_update(outputId, this.verification_req)
    },
    output_toggle(outputId) {
      webapi.output_toggle(outputId)
    }
  }
}
</script>

<style></style>
