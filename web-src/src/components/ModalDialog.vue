<template>
  <div>
    <transition name="fade">
      <div v-if="show" class="modal is-active">
        <div class="modal-background" @click="$emit('close')" />
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p v-if="title" class="title is-4">
                {{ title }}
              </p>
              <slot name="modal-content" />
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="$emit('close')">
                <span class="icon"><i class="mdi mdi-cancel" /></span>
                <span class="is-size-7">{{
                  close_action ? close_action : 'Cancel'
                }}</span>
              </a>
              <a
                v-if="delete_action"
                class="card-footer-item has-background-danger has-text-white has-text-weight-bold"
                @click="$emit('delete')"
              >
                <span class="icon"><i class="mdi mdi-delete" /></span>
                <span class="is-size-7">{{ delete_action }}</span>
              </a>
              <a
                v-if="ok_action"
                class="card-footer-item has-background-info has-text-white has-text-weight-bold"
                @click="$emit('ok')"
              >
                <span class="icon"><i class="mdi mdi-check" /></span>
                <span class="is-size-7">{{ ok_action }}</span>
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
export default {
  name: 'ModalDialog',
  props: ['show', 'title', 'ok_action', 'delete_action', 'close_action']
}
</script>

<style></style>
