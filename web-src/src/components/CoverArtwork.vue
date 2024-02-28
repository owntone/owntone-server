<template>
  <figure>
    <img
      v-lazy="{ src: artwork_url, lifecycle: lazy_lifecycle }"
      @click="$emit('click')"
    />
  </figure>
</template>

<script>
import { renderSVG } from '@/lib/SVGRenderer'

export default {
  name: 'CoverArtwork',
  props: {
    album: { default: '', type: String },
    artist: { default: '', type: String },
    artwork_url: { default: '', type: String },
  },
  emits: ['click'],

  data() {
    return {
      width: 600,
      height: 600,
      font_family: 'sans-serif',
      font_size: 200,
      font_weight: 600,
      lazy_lifecycle: {
        error: (el) => {
          el.src = this.dataURI()
        }
      }
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
      return renderSVG(this.caption, this.alt_text, {
        width: this.width,
        height: this.height,
        font_family: this.font_family,
        font_size: this.font_size,
        font_weight: this.font_weight
      })
    }
  }
}
</script>
