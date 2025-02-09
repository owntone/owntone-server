<template>
  <modal-dialog-playable :item="item" :show="show" @close="$emit('close')">
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
  </modal-dialog-playable>
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogArtist',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-artist',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
