<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
            <p v-if="title" class="title is-4" v-text="title" />
            <slot name="modal-content" />
          </div>
          <footer class="card-footer">
            <a class="card-footer-item has-text-dark" @click="$emit('close')">
              <mdicon class="icon" name="cancel" size="16" />
              <span class="is-size-7" v-text="close_action" />
            </a>
            <a
              v-if="delete_action"
              class="card-footer-item has-background-danger has-text-white has-text-weight-bold"
              @click="$emit('delete')"
            >
              <mdicon class="icon" name="delete" size="16" />
              <span class="is-size-7" v-text="delete_action" />
            </a>
            <a
              v-if="ok_action"
              class="card-footer-item has-background-info has-text-white has-text-weight-bold"
              @click="$emit('ok')"
            >
              <mdicon class="icon" name="check" size="16" />
              <span class="is-size-7" v-text="ok_action" />
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
</template>

<script>
export default {
  name: 'ModalDialog',
  props: {
    close_action: { default: '', type: String },
    delete_action: { default: '', type: String },
    ok_action: { default: '', type: String },
    show: Boolean,
    title: { required: true, type: String }
  },
  emits: ['delete', 'close', 'ok'],
  watch: {
    show() {
      const { classList } = document.querySelector('html')
      if (this.show) {
        classList.add('is-clipped')
      } else {
        classList.remove('is-clipped')
      }
    }
  }
}
</script>
