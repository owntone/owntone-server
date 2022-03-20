<template>
  <div class="fd-page-with-tabs">
    <tabs-settings />

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Remote Pairing</div>
      </template>

      <template #content>
        <!-- Paring request active -->
        <div v-if="pairing.active" class="notification">
          <form @submit.prevent="kickoff_pairing">
            <label class="label has-text-weight-normal">
              Remote pairing request from <b>{{ pairing.remote }}</b>
            </label>
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="pairing_req.pin"
                  class="input"
                  type="text"
                  placeholder="Enter pairing code"
                />
              </div>
              <div class="control">
                <button class="button is-info" type="submit">Send</button>
              </div>
            </div>
          </form>
        </div>
        <!-- No pairing requests -->
        <div v-if="!pairing.active" class="content">
          <p>No active pairing request.</p>
        </div>
      </template>
    </content-with-heading>

    <content-with-heading>
      <template #heading-left>
        <div class="title is-4">Speaker pairing and device verification</div>
      </template>

      <template #content>
        <p class="content">
          If your speaker requires pairing then activate it below and enter the
          PIN that it displays.
        </p>

        <div v-for="output in outputs" :key="output.id">
          <div class="field">
            <div class="control">
              <label class="checkbox">
                <input
                  v-model="output.selected"
                  type="checkbox"
                  @change="output_toggle(output.id)"
                />
                {{ output.name }}
              </label>
            </div>
          </div>
          <form
            v-if="output.needs_auth_key"
            class="fd-has-margin-bottom"
            @submit.prevent="kickoff_verification(output.id)"
          >
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="verification_req.pin"
                  class="input"
                  type="text"
                  placeholder="Enter verification code"
                />
              </div>
              <div class="control">
                <button class="button is-info" type="submit">Verify</button>
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
  name: 'SettingsPageRemotesOutputs',
  components: { ContentWithHeading, TabsSettings },

  filters: {},

  data() {
    return {
      pairing_req: { pin: '' },
      verification_req: { pin: '' }
    }
  },

  computed: {
    pairing() {
      return this.$store.state.pairing
    },

    outputs() {
      return this.$store.state.outputs
    }
  },

  methods: {
    kickoff_pairing() {
      webapi.pairing_kickoff(this.pairing_req)
    },

    output_toggle(outputId) {
      webapi.output_toggle(outputId)
    },

    kickoff_verification(outputId) {
      webapi.output_update(outputId, this.verification_req)
    }
  }
}
</script>

<style></style>
