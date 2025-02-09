<template>
  <modal-dialog-playable
    :expression="expression"
    :show="show"
    @close="$emit('close')"
  >
    <template #content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.genre.albums')"
        />
        <div class="title is-6" v-text="item.album_count" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.genre.tracks')"
        />
        <div class="title is-6" v-text="item.track_count" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.genre.duration')"
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
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogGenre',
  components: { ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    media_kind: { required: true, type: String },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    expression() {
      return `genre is "${this.item.name}" and media_kind is ${this.media_kind}`
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.name },
        query: { media_kind: this.media_kind }
      })
    }
  }
}
</script>
