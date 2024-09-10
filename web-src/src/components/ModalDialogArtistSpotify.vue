<template>
  <base-modal :show="show" @close="$emit('close')">
    <template #content>
      <p class="title is-4">
        <a class="has-text-link" @click="open" v-text="item.name" />
      </p>
      <div class="content is-small">
        <p>
          <span
            class="heading"
            v-text="$t('dialog.spotify.artist.popularity')"
          />
          <span
            class="title is-6"
            v-text="[item.popularity, item.followers.total].join(' / ')"
          />
        </p>
        <p>
          <span class="heading" v-text="$t('dialog.spotify.artist.genres')" />
          <span class="title is-6" v-text="item.genres.join(', ')" />
        </p>
      </div>
    </template>
    <template #footer>
      <a class="card-footer-item has-text-dark" @click="queue_add">
        <mdicon class="icon" name="playlist-plus" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.artist.add')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="queue_add_next">
        <mdicon class="icon" name="playlist-play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.artist.add-next')" />
      </a>
      <a class="card-footer-item has-text-dark" @click="play">
        <mdicon class="icon" name="play" size="16" />
        <span class="is-size-7" v-text="$t('dialog.spotify.artist.play')" />
      </a>
    </template>
  </base-modal>
</template>

<script>
import BaseModal from '@/components/BaseModal.vue'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogArtistSpotify',
  components: { BaseModal },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],

  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
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
