<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p
                class="title is-4"
                v-text="$t('dialog.remote-pairing.title')"
              />
              <form @submit.prevent="kickoff_pairing">
                <label class="label" v-text="pairing.remote" />
                <div class="field">
                  <div class="control">
                    <input
                      ref="pin_field"
                      v-model="pairing_req.pin"
                      class="input"
                      type="text"
                      :placeholder="$t('dialog.remote-pairing.pairing-code')"
                    />
                  </div>
                </div>
              </form>
            </div>
            <footer class="card-footer is-clipped">
              <a
                class="card-footer-item has-text-danger"
                @click="$emit('close')"
              >
                <mdicon class="icon" name="cancel" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.remote-pairing.cancel')"
                />
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="kickoff_pairing"
              >
                <mdicon class="icon" name="cellphone" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.remote-pairing.pair')"
                />
              </a>
            </footer>
          </div>
        </div>
        <button
          class="modal-close is-large"
          aria-label="close"
          @click="$emit('close')"
        />
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogRemotePairing',
  props: ['show'],
  emits: ['close'],

  data() {
    return {
      pairing_req: { pin: '' }
    }
  },

  computed: {
    pairing() {
      return this.$store.state.pairing
    }
  },

  watch: {
    show() {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
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

<style></style>
