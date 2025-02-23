<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card is-shadowless">
          <div class="card-content">
            <p v-if="title" class="title is-4" v-text="title" />
            <slot name="content" />
          </div>
          <footer v-if="actions.length" class="card-footer">
            <a
              v-for="action in actions"
              :key="action.label"
              class="card-footer-item"
              :class="{ 'is-disabled': action.disabled }"
              @click="action.handler"
            >
              <mdicon class="icon" :name="action.icon" size="16" />
              <span class="is-size-7" v-text="action.label" />
            </a>
          </footer>
        </div>
      </div>
    </div>
  </transition>
</template>

<script>
export default {
  name: 'ModalDialog',
  props: {
    actions: { type: Array, required: true },
    show: Boolean,
    title: { type: String, default: '' }
  },
  emits: ['close'],
  watch: {
    show(value) {
      const { classList } = document.querySelector('html')
      if (value) {
        classList.add('is-clipped')
      } else {
        classList.remove('is-clipped')
      }
    }
  }
}
</script>

<style scoped>
.fade-leave-active {
  transition: opacity 0.2s ease;
}
.fade-enter-active {
  transition: opacity 0.5s ease;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
.fade-enter-to,
.fade-leave-from {
  opacity: 1;
}
.card-content {
  max-height: calc(100vh - calc(4 * var(--bulma-navbar-height)));
  overflow: auto;
}
</style>
