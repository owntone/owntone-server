<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <div class="title is-4">
        <a @click="open" v-text="item.name" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.artist.albums')"
        />
        <div class="title is-6" v-text="item.album_count" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.artist.tracks')"
        />
        <div class="title is-6" v-text="item.track_count" />
      </div>
      <div class="mb-3">
        <div class="is-size-7 is-uppercase" v-text="$t('dialog.artist.type')" />
        <div class="title is-6" v-text="$t(`data.kind.${item.data_kind}`)" />
      </div>
      <div class="mb-3">
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('dialog.artist.added-on')"
        />
        <div class="title is-6" v-text="$filters.datetime(item.time_added)" />
      </div>
    </template>
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.artist.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.artist.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.artist.play')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogArtist',
  components: { BaseModal },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],

  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-artist',
        params: { id: this.item.id }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play_uri(this.item.uri, false)
    },
    queue_add() {
      this.$emit('close')
      webapi.queue_add(this.item.uri)
    },
    queue_add_next() {
      this.$emit('close')
      webapi.queue_add_next(this.item.uri)
    }
  }
}
</script>
