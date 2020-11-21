<template>
  <figure>
    <img v-lazyload
      :data-src="artwork_url_with_size"
      :data-err="dataURI"
      :key="artwork_url_with_size"
      @click="$emit('click')">
  </figure>
</template>

<script>
import webapi from '@/webapi'
import SVGRenderer from '@/lib/SVGRenderer'
import stringToColor from 'string-to-color'

export default {
  name: 'CoverArtwork',
  props: ['artist', 'album', 'artwork_url', 'maxwidth', 'maxheight'],

  data () {
    return {
      svg: new SVGRenderer(),
      width: 600,
      height: 600,
      font_family: 'sans-serif',
      font_size: 200,
      font_weight: 600
    }
  },

  computed: {
    artwork_url_with_size: function () {
      if (this.maxwidth > 0 && this.maxheight > 0) {
        return webapi.artwork_url_append_size_params(this.artwork_url, this.maxwidth, this.maxheight)
      }
      return webapi.artwork_url_append_size_params(this.artwork_url)
    },

    alt_text () {
      return this.artist + ' - ' + this.album
    },

    caption () {
      if (this.album) {
        return this.album.substring(0, 2)
      }
      if (this.artist) {
        return this.artist.substring(0, 2)
      }
      return ''
    },

    background_color () {
      return stringToColor(this.alt_text)
    },

    is_background_light () {
      // Based on https://stackoverflow.com/a/44615197
      const hex = this.background_color.replace(/#/, '')
      const r = parseInt(hex.substr(0, 2), 16)
      const g = parseInt(hex.substr(2, 2), 16)
      const b = parseInt(hex.substr(4, 2), 16)

      const luma = [
        0.299 * r,
        0.587 * g,
        0.114 * b
      ].reduce((a, b) => a + b) / 255

      return luma > 0.5
    },

    text_color () {
      return this.is_background_light ? '#000000' : '#ffffff'
    },

    rendererParams () {
      return {
        width: this.width,
        height: this.height,
        textColor: this.text_color,
        backgroundColor: this.background_color,
        caption: this.caption,
        fontFamily: this.font_family,
        fontSize: this.font_size,
        fontWeight: this.font_weight
      }
    },

    dataURI () {
      return this.svg.render(this.rendererParams)
    }
  }
}
</script>
