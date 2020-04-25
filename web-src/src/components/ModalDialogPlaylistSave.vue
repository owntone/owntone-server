<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4">
                Save queue to playlist
              </p>
              <form v-on:submit.prevent="save" class="fd-has-margin-bottom">
                <div class="field">
                  <p class="control is-expanded has-icons-left">
                    <input class="input is-shadowless" type="text" placeholder="Playlist name" v-model="playlist_name" :disabled="loading" ref="playlist_name_field">
                    <span class="icon is-left">
                      <i class="mdi mdi-file-music"></i>
                    </span>
                  </p>
                </div>
              </form>
            </div>
            <footer class="card-footer" v-if="loading">
              <a class="card-footer-item has-text-dark">
                <span class="icon"><i class="mdi mdi-web"></i></span> <span class="is-size-7">Saving ...</span>
              </a>
            </footer>
            <footer class="card-footer" v-else>
              <a class="card-footer-item has-text-danger" @click="$emit('close')">
                <span class="icon"><i class="mdi mdi-cancel"></i></span> <span class="is-size-7">Cancel</span>
              </a>
              <a class="card-footer-item has-background-info has-text-white has-text-weight-bold" @click="save">
                <span class="icon"><i class="mdi mdi-content-save"></i></span> <span class="is-size-7">Save</span>
              </a>
            </footer>
          </div>
        </div>
        <button class="modal-close is-large" aria-label="close" @click="$emit('close')"></button>
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialogPlaylistSave',
  props: ['show'],

  data () {
    return {
      playlist_name: '',
      loading: false
    }
  },

  methods: {
    save: function () {
      if (this.playlist_name.length < 1) {
        return
      }

      this.loading = true
      webapi.queue_save_playlist(this.playlist_name).then(() => {
        this.$emit('close')
        this.playlist_name = ''
      }).catch(() => {
        this.loading = false
      })
    }
  },

  watch: {
    'show' () {
      if (this.show) {
        this.loading = false

        // We need to delay setting the focus to the input field until the field is part of the dom and visible
        setTimeout(() => {
          this.$refs.playlist_name_field.focus()
        }, 10)
      }
    }
  }
}
</script>

<style>
</style>
