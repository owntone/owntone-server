<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-card">
          <section class="modal-card-body">
            <form v-on:submit.prevent="save">
              <div class="field">
                <p class="control is-expanded has-icons-left">
                  <input class="input is-rounded is-shadowless" type="text" placeholder="playlist name" v-model="pls_name" ref="pls_name_field">
                  <span class="icon is-left">
                    <i class="mdi mdi-file-music"></i>
                  </span>
                </p>
              </div>
            </form>
          </section>
          <footer class="modal-card-foot">
            <button class="button is-success" @click="save" :disabled="pls_name.length < 3">Save</button>
            <button class="button" @click="$emit('close')">Cancel</button>
          </footer>
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
  props: [ 'show' ],

  data () {
    return {
      pls_name: ''
    }
  },

  methods: {
    save: function () {
      this.$emit('close')
      webapi.queue_save_playlist(this.pls_name)
    }
  }
}
</script>

<style>
</style>
