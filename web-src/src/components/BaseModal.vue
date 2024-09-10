<template>
  <transition name="fade">
    <div v-if="show" class="modal is-active">
      <div class="modal-background" @click="$emit('close')" />
      <div class="modal-content">
        <div class="card">
          <div class="card-content">
            <slot name="content" />
          </div>
          <footer class="card-footer is-clipped">
            <slot name="footer" />
          </footer>
        </div>
      </div>
    </div>
  </transition>
</template>

<script>
export default {
  name: 'BaseModal',
  props: {
    show: Boolean
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
