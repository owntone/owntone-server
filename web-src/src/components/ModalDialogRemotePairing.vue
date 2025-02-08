<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    @cancel="$emit('close')"
    @close="$emit('close')"
    @pair="pair"
  >
    <template #modal-content>
      <p class="title is-4" v-text="$t('dialog.remote-pairing.title')" />
      <form @submit.prevent="pair">
        <label class="label" v-text="pairing.remote" />
        <div class="field">
          <div class="control">
            <input
              ref="pin_field"
              v-model="pairing_req.pin"
              class="input"
              inputmode="numeric"
              pattern="[\d]{4}"
              :placeholder="$t('dialog.remote-pairing.pairing-code')"
            />
          </div>
        </div>
      </form>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import { useRemotesStore } from '@/stores/remotes'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogRemotePairing',
  components: { ModalDialog },
  props: { show: Boolean },
  emits: ['close'],
  setup() {
    return { remoteStore: useRemotesStore() }
  },
  data() {
    return {
      pairing_req: { pin: '' }
    }
  },
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.remote-pairing.cancel'),
          event: 'cancel',
          icon: 'cancel'
        },
        {
          label: this.$t('dialog.remote-pairing.pair'),
          event: 'pair',
          icon: 'cellphone'
        }
      ]
    },
    pairing() {
      return this.remoteStore.pairing
    }
  },
  watch: {
    show() {
      if (this.show) {
        this.loading = false
        // Delay setting the focus on the input field until it is part of the DOM and visible
        setTimeout(() => {
          this.$refs.pin_field.focus()
        }, 10)
      }
    }
  },
  methods: {
    pair() {
      webapi.pairing_kickoff(this.pairing_req).then(() => {
        this.pairing_req.pin = ''
      })
    }
  }
}
</script>
