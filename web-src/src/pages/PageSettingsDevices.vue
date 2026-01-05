<template>
  <tabs-settings />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('settings.devices.pairing') }" />
    </template>
    <template #content>
      <form v-if="remotesStore.active" @submit.prevent="pairRemote">
        <label class="label has-text-weight-normal content">
          <span v-text="$t('settings.devices.pairing-request')" />
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
      <div v-else v-text="$t('settings.devices.no-active-pairing')" />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('settings.devices.speaker-pairing') }"
      />
    </template>
    <template #content>
      <div
        class="content"
        v-text="$t('settings.devices.speaker-pairing-info')"
      />
      <div
        v-for="output in outputsStore.outputs"
        :key="output.id"
        class="field columns is-multiline"
      >
        <div class="column is-flex is-one-third">
          <control-switch
            v-model="output.selected"
            @update:model-value="toggleOutput(output.id)"
          >
            <template #label>
              <span v-text="output.name" />
            </template>
          </control-switch>
        </div>
        <div class="column is-one-third">
          <control-integer-field
            v-model="output.offset_ms"
            :min="-2000"
            :max="2000"
            :step="50"
            @update:model-value="
              (value) => onOutputOffsetChange(output.id, value)
            "
          >
            <mdicon class="icon is-small is-left" name="timelapse" size="16" />
            <span class="icon is-right">ms</span>
          </control-integer-field>
        </div>
        <div class="column is-one-third">
          <form
            v-if="output.needs_auth_key"
            @submit.prevent="pairOutput(output.id)"
          >
            <control-pin-field
              class="has-addons"
              :placeholder="$t('settings.devices.verification-code')"
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
      </div>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlIntegerField from '@/components/ControlIntegerField.vue'
import ControlPinField from '@/components/ControlPinField.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSettings from '@/components/TabsSettings.vue'
import outputs from '@/api/outputs'
import remotes from '@/api/remotes'
import { useOutputsStore } from '@/stores/outputs'
import { useRemotesStore } from '@/stores/remotes'

export default {
  name: 'PageSettingsDevices',
  components: {
    ContentWithHeading,
    ControlIntegerField,
    ControlPinField,
    ControlSwitch,
    PaneTitle,
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
  methods: {
    onOutputPinChange(pin) {
      this.outputPin = pin
    },
    onOutputOffsetChange(identifier, value) {
      outputs.update(identifier, { offset_ms: value })
    },
    onRemotePinChange(pin, disabled) {
      this.remotePin = pin
      this.remotePairingDisabled = disabled
    },
    pairOutput(identifier) {
      outputs.update(identifier, { pin: this.outputPin })
    },
    pairRemote() {
      remotes.pair(this.remotePin)
    },
    toggleOutput(identifier) {
      outputs.toggle(identifier)
    }
  }
}
</script>
