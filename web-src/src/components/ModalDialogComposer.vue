<template>
  <modal-dialog-playable
    :expression="expression"
    :show="show"
    @close="$emit('close')"
  >
    <template #content>
      <div class="title is-4">
        <a @click="open_albums" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.albums')"
        />
        <div class="title is-6">
          <a @click="open_albums" v-text="item.album_count" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.tracks')"
        />
        <div class="title is-6">
          <a @click="open_tracks" v-text="item.track_count" />
        </div>
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.composer.duration')"
        />
        <div
          class="title is-6"
          v-text="$filters.durationInHours(item.length_ms)"
        />
      </div>
    </template>
  </modal-dialog-playable>
</template>

<script>
import ModalDialogPlayable from './ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogComposer',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    expression() {
      return `composer is "${this.item.name}" and media_kind is music`
    }
  },
  methods: {
    open_albums() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.item.name }
      })
    },
    open_tracks() {
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.item.name }
      })
    }
  }
}
</script>
