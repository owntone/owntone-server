<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <p class="title is-4" v-text="$t('dialog.remote-pairing.title')" />
      <form @submit.prevent="kickoff_pairing">
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
    <template #footer>
      <a class="card-footer-item has-text-danger" @click="$emit('close')">
        <mdicon class="icon" name="cancel" size="16" />
        <span class="is-size-7" v-text="$t('dialog.remote-pairing.cancel')" />
      </a>
      <a class="card-footer-item" @click="kickoff_pairing">
        <mdicon class="icon" name="cellphone" size="16" />
        <span class="is-size-7" v-text="$t('dialog.remote-pairing.pair')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import { useRemotesStore } from '@/stores/remotes'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogRemotePairing',
  components: { BaseModal },
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
    kickoff_pairing() {
      webapi.pairing_kickoff(this.pairing_req).then(() => {
        this.pairing_req.pin = ''
      })
    }
  }
}
</script>
