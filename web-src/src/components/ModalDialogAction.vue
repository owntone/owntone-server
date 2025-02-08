<template>
  <modal-dialog :show="show">
    <template #content>
      <p v-if="title" class="title is-4" v-text="title" />
      <slot name="modal-content" />
    </template>
    <template #footer>
      <a
        v-for="action in actions"
        :key="action.event"
        class="card-footer-item"
        :class="{ 'is-disabled': action.disabled }"
        @click="$emit(action.event)"
      >
        <mdicon class="icon" :name="action.icon" size="16" />
        <span class="is-size-7" v-text="action.label" />
      </a>
    </template>
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'

export default {
  name: 'ModalDialogAction',
  components: { ModalDialog },
  props: {
    actions: { type: Array, required: true },
    show: Boolean,
    title: { default: '', type: String }
  },
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
