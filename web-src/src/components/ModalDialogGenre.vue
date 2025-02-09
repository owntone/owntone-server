<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
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
  </modal-dialog>
</template>

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogGenre',
  components: { ModalDialog },
  props: {
    item: { required: true, type: Object },
    media_kind: { required: true, type: String },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    actions() {
      return [
        {
          label: this.$t('dialog.genre.add'),
          handler: this.queue_add,
          icon: 'playlist-plus'
        },
        {
          label: this.$t('dialog.genre.add-next'),
          handler: this.queue_add_next,
          icon: 'playlist-play'
        },
        {
          label: this.$t('dialog.genre.play'),
          handler: this.play,
          icon: 'play'
        }
      ]
    },
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
    },
    play() {
      this.$emit('close')
      webapi.player_play_expression(this.expression, false)
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_expression_add(this.expression)
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_expression_add_next(this.expression)
    }
  }
}
</script>
