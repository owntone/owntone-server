<template>
  <figure>
    <img
      v-lazy="{ src: artwork_url_with_size, lifecycle: lazy_lifecycle }"
      @click="$emit('click')"
    />
  </figure>
</template>

<script>
import webapi from '@/webapi'
import { renderSVG } from '@/lib/SVGRenderer'

export default {
  name: 'CoverArtwork',
  props: ['artist', 'album', 'artwork_url', 'maxwidth', 'maxheight'],

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
    artwork_url_with_size: function () {
      if (this.maxwidth > 0 && this.maxheight > 0) {
        return webapi.artwork_url_append_size_params(
          this.artwork_url,
          this.maxwidth,
          this.maxheight
        )
      }
      return webapi.artwork_url_append_size_params(this.artwork_url)
    },

    alt_text() {
      return this.artist + ' - ' + this.album
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
    dataURI: function () {
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
