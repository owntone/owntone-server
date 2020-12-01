<template>
  <div>
    <transition name="fade">
      <div class="modal is-active" v-if="show">
        <div class="modal-background" @click="$emit('close')"></div>
        <div class="modal-content fd-modal-card">
          <div class="card">
            <div class="card-content">
              <p class="title is-4"> {{ title }}</p>
              <div class="content is-small">
                <p>
                  <span class="heading">Albums</span>
                  <span class="title is-6">{{ albums.total }}</span>
                </p>
                <p>
                  <span class="heading">Artists</span>
                  <span class="title is-6">{{ artist_count }}</span>
                </p>
                <p>
                  <span class="heading">Tracks</span>
                  <span class="title is-6">{{ track_count }}</span>
                </p>
              </div>
            </div>
            <footer class="card-footer">
              <a class="card-footer-item has-text-dark" @click="queue_add">
                <span class="icon"><i class="mdi mdi-playlist-plus"></i></span> <span class="is-size-7">Add</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="queue_add_next">
                <span class="icon"><i class="mdi mdi-playlist-play"></i></span> <span class="is-size-7">Add Next</span>
              </a>
              <a class="card-footer-item has-text-dark" @click="play">
                <span class="icon"><i class="mdi mdi-play"></i></span> <span class="is-size-7">Play</span>
              </a>
            </footer>
          </div>
        </div>
        <button class="modal-close is-large" aria-label="close" @click="$emit('close')"></button>
      </div>
    </transition>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ModalDialog',
  props: ['show', 'title', 'albums'],

  computed: {
    uris () {
      return this.albums.items.map(a => a.uri).join(',')
    },
    artist_count () {
      return new Set(this.albums.items.map(a => a.artist_id)).size
    },
    track_count () {
      return this.albums.items.reduce((acc, item) => {
        acc += item.track_count
        return acc
      }, 0)
    }
  },

  methods: {
    play: function () {
      this.$emit('close')
      webapi.player_play_uri(this.uris, false)
    },

    queue_add: function () {
      this.$emit('close')
      webapi.queue_add(this.uris)
    },

    queue_add_next: function () {
      this.$emit('close')
      webapi.queue_add_next(this.uris)
    }
  }
}
</script>

<style>
</style>
