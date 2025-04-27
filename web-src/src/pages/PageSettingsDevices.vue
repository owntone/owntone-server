<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.devices.pairing') }"
      />
    </template>
    <template #content>
      <form v-if="remotesStore.active" @submit.prevent="pairRemote">
        <label class="label has-text-weight-normal content">
          <span v-text="$t('page.settings.devices.pairing-request')" />
          <b v-text="remotesStore.remote" />
        </label>
        <control-pin-field
          :placeholder="$t('dialog.remote-pairing.pairing-code')"
          @input="onRemotePinChange"
        >
          <div class="control">
            <button
              class="button"
              type="submit"
              :disabled="remotePairingDisabled"
              v-text="$t('actions.verify')"
            />
          </div>
        </control-pin-field>
      </form>
      <div v-else v-text="$t('page.settings.devices.no-active-pairing')" />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.settings.devices.speaker-pairing') }"
      />
    </template>
    <template #content>
      <div
        class="content"
        v-text="$t('page.settings.devices.speaker-pairing-info')"
      />
      <div v-for="output in outputs" :key="output.id" class="field is-grouped">
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
          @submit.prevent="pairOutput(output.id)"
        >
          <control-pin-field
            :placeholder="$t('page.settings.devices.verification-code')"
            @input="onOutputPinChange"
          >
            <div class="control">
              <button
                class="button"
                type="submit"
                v-text="$t('actions.verify')"
              />
            </div>
          </control-pin-field>
        </form>
      </div>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlPinField from '@/components/ControlPinField.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import { useOutputsStore } from '@/stores/outputs'
import { useRemotesStore } from '@/stores/remotes'
import webapi from '@/webapi'

export default {
  name: 'PageSettingsDevices',
  components: {
    ContentWithHeading,
    ControlPinField,
    ControlSwitch,
    HeadingTitle,
    TabsSettings
  },
  setup() {
    return { outputsStore: useOutputsStore(), remotesStore: useRemotesStore() }
  },
  data() {
    return {
      outputPin: '',
      remotePairingDisabled: true,
      remotePin: ''
    }
  },
  computed: {
    outputs() {
      return this.outputsStore.outputs
    }
  },
  methods: {
    pairRemote() {
      webapi.pairing_kickoff({ pin: this.remotePin })
    },
    pairOutput(identifier) {
      webapi.output_update(identifier, { pin: this.outputPin })
    },
    onRemotePinChange(pin, disabled) {
      this.remotePin = pin
      this.remotePairingDisabled = disabled
    },
    onOutputPinChange(pin) {
      this.outputPin = pin
    },
    toggleOutput(identifier) {
      webapi.output_toggle(identifier)
    }
  }
}
</script>
