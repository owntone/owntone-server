<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-card">
          <section class="modal-card-body">
            <form v-on:submit.prevent="add_stream">
              <div class="field">
                <p class="control is-expanded has-icons-left">
                  <input class="input is-rounded is-shadowless" type="text" placeholder="URL stream" v-model="url" ref="url_field">
                  <span class="icon is-left">
                    <i class="mdi mdi-file-music"></i>
                  </span>
                </p>
              </div>
            </form>
          </section>
          <footer class="modal-card-foot">
            <button class="button is-success" @click="add_stream" :disabled="url.length < 9">Add</button>
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
  name: 'ModalDialogAddUrlStream',
  props: [ 'show' ],

  data () {
    return {
      url: ''
    }
  },

  methods: {
    add_stream: function () {
      this.$emit('close')
      webapi.queue_add(this.url)
      this.url = ''
    }
  }
}
</script>

<style>
</style>
