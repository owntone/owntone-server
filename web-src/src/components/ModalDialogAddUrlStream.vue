<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-text="$t('dialog.add.stream.title')" />
              <form class="mb-5" @submit.prevent="play">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input
                      ref="url_field"
                      v-model="url"
                      class="input is-shadowless"
                      type="text"
                      :placeholder="$t('dialog.add.stream.placeholder')"
                      :disabled="loading"
                    />
                    <mdicon class="icon is-left" name="web" size="16" />
                  </p>
                </div>
              </form>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item has-text-dark">
                <mdicon class="icon" name="web" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.add.stream.loading')"
                />
              </a>
            </footer>
            <footer v-else class="card-footer is-clipped">
              <a
                class="card-footer-item has-text-danger"
                @click="$emit('close')"
              >
                <mdicon class="icon" name="cancel" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.add.stream.cancel')"
                />
              </a>
              <a class="card-footer-item has-text-dark" @click="add_stream">
                <mdicon class="icon" name="playlist-plus" size="16" />
                <span class="is-size-7" v-text="$t('dialog.add.stream.add')" />
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="play"
              >
                <mdicon class="icon" name="play" size="16" />
                <span class="is-size-7" v-text="$t('dialog.add.stream.play')" />
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
  name: 'ModalDialogAddUrlStream',
  props: ['show'],
  emits: ['close'],

  data() {
    return {
      url: '',
      loading: false
    }
  },

  watch: {
    show() {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
        setTimeout(() => {
          this.$refs.url_field.focus()
        }, 10)
      }
    }
  },

  methods: {
    add_stream() {
      this.loading = true
      webapi
        .queue_add(this.url)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    },

    play() {
      this.loading = true
      webapi
        .player_play_uri(this.url, false)
        .then(() => {
          this.$emit('close')
          this.url = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>

<style></style>
