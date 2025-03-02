<template>
  <div>
    <tabs-settings />
    <content-with-heading>
      <template #heading-left>
        <heading-title
          :content="{ title: $t('page.settings.devices.pairing') }"
        />
      </template>
      <template #content>
        <div v-if="pairing.active">
          <form @submit.prevent="kickoff_pairing">
            <label class="label has-text-weight-normal content">
              <span v-text="$t('page.settings.devices.pairing-request')" />
              <b v-text="pairing.remote" />
            </label>
            <div class="field is-grouped">
              <div class="control">
                <input
                  v-model="pairing_req.pin"
                  class="input"
                  inputmode="numeric"
                  pattern="[\d]{4}"
                  :placeholder="$t('page.settings.devices.pairing-code')"
                />
              </div>
              <div class="control">
                <button
                  class="button"
                  type="submit"
                  v-text="$t('page.settings.devices.send')"
                />
              </div>
            </div>
          </form>
        </div>
        <div v-else>
          <p v-text="$t('page.settings.devices.no-active-pairing')" />
        </div>
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <heading-title
          :content="{ title: $t('page.settings.devices.speaker-pairing') }"
        />
      </template>
      <template #content>
        <p
          class="content"
          v-text="$t('page.settings.devices.speaker-pairing-info')"
        />
        <div v-for="output in outputs" :key="output.id">
          <control-switch
            v-model="output.selected"
            @update:model-value="toggleOutput(output.id)"
          >
            <template #label>
              <span v-text="output.name" />
            </template>
          </control-switch>
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
                  inputmode="numeric"
                  pattern="[\d]{4}"
                  :placeholder="$t('page.settings.devices.verification-code')"
                />
              </div>
              <div class="control">
                <button
                  class="button"
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
import ControlSwitch from '@/components/ControlSwitch.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useOutputsStore } from '@/stores/outputs'
import { useRemotesStore } from '@/stores/remotes'
import webapi from '@/webapi'

export default {
  name: 'PageSettingsRemotesOutputs',
  components: { ContentWithHeading, ControlSwitch, HeadingTitle, TabsSettings },
  setup() {
    return { outputsStore: useOutputsStore(), remotesStore: useRemotesStore() }
  },
  data() {
    return {
      pairing_req: { pin: '' },
      verification_req: { pin: '' }
    }
  },
  computed: {
    outputs() {
      return this.outputsStore.outputs
    },
    pairing() {
      return this.remotesStore.pairing
    }
  },
  methods: {
    kickoff_pairing() {
      webapi.pairing_kickoff(this.pairing_req)
    },
    kickoff_verification(identifier) {
      webapi.output_update(identifier, this.verification_req)
    },
    toggleOutput(identifier) {
      webapi.output_toggle(identifier)
    }
  }
}
</script>
