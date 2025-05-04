<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="$t('dialog.remote-pairing.title')"
    @close="$emit('close')"
  >
    <template #content>
      <form @submit.prevent="pair">
        <label class="label" v-text="remoteStore.remote" />
        <control-pin-field
          :placeholder="$t('dialog.remote-pairing.pairing-code')"
          @input="onPinChange"
        />
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ControlPinField from '@/components/ControlPinField.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import remotes from '@/api/remotes'
import { useRemotesStore } from '@/stores/remotes'

export default {
  name: 'ModalDialogRemotePairing',
  components: { ControlPinField, ModalDialog },
  props: { show: Boolean },
  emits: ['close'],
  setup() {
    return { remoteStore: useRemotesStore() }
  },
  data() {
    return {
      disabled: true,
      pin: ''
    }
  },
  computed: {
    actions() {
      return [
        { handler: this.cancel, icon: 'cancel', key: 'actions.cancel' },
        {
          disabled: this.disabled,
          handler: this.pair,
          icon: 'vector-link',
          key: 'actions.pair'
        }
      ]
    }
  },
  methods: {
    cancel() {
      this.$emit('close')
    },
    onPinChange(pin, disabled) {
      this.pin = pin
      this.disabled = disabled
    },
    pair() {
      remotes.pair(this.pin).then(() => {
        this.pin = ''
      })
    }
  }
}
</script>
