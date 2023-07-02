<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-text="$t('dialog.playlist.save.title')" />
              <form class="mb-5" @submit.prevent="save">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input
                      ref="playlist_name_field"
                      v-model="playlist_name"
                      class="input is-shadowless"
                      type="text"
                      :placeholder="$t('dialog.playlist.save.playlist-name')"
                      :disabled="loading"
                    />
                    <mdicon class="icon is-left" name="file-music" size="16" />
                  </p>
                </div>
              </form>
            </div>
            <footer v-if="loading" class="card-footer">
              <a class="card-footer-item has-text-dark">
                <mdicon class="icon" name="web" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.playlist.save.saving')"
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
                  v-text="$t('dialog.playlist.save.cancel')"
                />
              </a>
              <a
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="save"
              >
                <mdicon class="icon" name="content-save" size="16" />
                <span
                  class="is-size-7"
                  v-text="$t('dialog.playlist.save.save')"
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
  name: 'ModalDialogPlaylistSave',
  props: ['show'],
  emits: ['close'],

  data() {
    return {
      playlist_name: '',
      loading: false
    }
  },

  watch: {
    show() {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
        setTimeout(() => {
          this.$refs.playlist_name_field.focus()
        }, 10)
      }
    }
  },

  methods: {
    save() {
      if (this.playlist_name.length < 1) {
        return
      }

      this.loading = true
      webapi
        .queue_save_playlist(this.playlist_name)
        .then(() => {
          this.$emit('close')
          this.playlist_name = ''
        })
        .catch(() => {
          this.loading = false
        })
    }
  }
}
</script>

<style></style>
