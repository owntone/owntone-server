<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4" v-if="title">
                {{ title }}
              </p>
              <slot name="modal-content"></slot>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="$emit('close')">
                <span class="icon"><i class="mdi mdi-cancel"></i></span> <span class="is-size-7">{{ close_action ? close_action : 'Cancel' }}</span>
              </a>
              <a v-if="delete_action" class="card-footer-item has-background-danger has-text-white has-text-weight-bold" @click="$emit('delete')">
                <span class="icon"><i class="mdi mdi-delete"></i></span> <span class="is-size-7">{{ delete_action }}</span>
              </a>
              <a v-if="ok_action" class="card-footer-item has-background-info has-text-white has-text-weight-bold" @click="$emit('ok')">
                <span class="icon"><i class="mdi mdi-check"></i></span> <span class="is-size-7">{{ ok_action }}</span>
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
export default {
  name: 'ModalDialog',
  props: ['show', 'title', 'ok_action', 'delete_action', 'close_action']
}
</script>

<style>
</style>
