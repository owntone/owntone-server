<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <p class="title is-4" v-text="item" />
    </template>
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.directory.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.directory.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.directory.play')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogDirectory',
  components: { BaseModal },
  props: { item: { required: true, type: String }, show: Boolean },
  emits: ['close'],

  methods: {
    play() {
      this.$emit('close')
      webapi.player_play_expression(
        `path starts with "${this.item}" order by path asc`,
        false
      )
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(
        `path starts with "${this.item}" order by path asc`
      )
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(
        `path starts with "${this.item}" order by path asc`
      )
    }
  }
}
</script>
