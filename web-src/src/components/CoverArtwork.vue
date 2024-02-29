<template>
  <figure>
    <img v-lazy="{ src: artwork_url, lifecycle }" @click="$emit('click')" />
  </figure>
</template>

<script>
import { renderSVG } from '@/lib/SVGRenderer'

export default {
  name: 'CoverArtwork',
  props: {
    album: { default: '', type: String },
    artist: { default: '', type: String },
    artwork_url: { default: '', type: String }
  },
  emits: ['click'],

  data() {
    return {
      font: { family: 'sans-serif', weight: 'bold' },
      lifecycle: {
        error: (el) => {
          el.src = this.dataURI()
        }
      },
      size: 600
    }
  },

  computed: {
    alt_text() {
      return `${this.artist} - ${this.album}`
    },

    caption() {
      if (this.album) {
        return this.album.substring(0, 2)
      }
      if (this.artist) {
        return this.artist.substring(0, 2)
      }
      return ''
    }
  },

  methods: {
    dataURI() {
      return renderSVG({
        alternate: this.alt_text,
        caption: this.caption,
        font: this.font,
        size: this.size
      })
    }
  }
}
</script>
