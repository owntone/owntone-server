<template>
  <div class="navbar-item">
    <div class="level is-mobile">
      <div class="level-left is-flex-grow-1">
        <div class="level-item is-flex-grow-0">
          <a class="button is-white is-small">
            <span
              class="icon is-clickable"
              :class="{ 'has-text-grey-light': !output.selected }"
              @click="set_enabled"
              ><mdicon :name="type_class" size="18" :title="output.type"
            /></span>
          </a>
        </div>
        <div class="level-item">
          <div class="is-flex-grow-1">
            <p
              class="heading"
              :class="{ 'has-text-grey-light': !output.selected }"
              v-text="output.name"
            />
            <input
              v-model="volume"
              :disabled="!output.selected"
              class="slider"
              :class="{ 'is-inactive': !output.selected }"
              max="100"
              type="range"
              :style="{
                '--ratio': volume / 100,
                '--cursor': $filters.cursor(this.cursor)
              }"
              @change="change_volume"
            />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { mdiCancel } from '@mdi/js'
import webapi from '@/webapi'

export default {
  name: 'NavbarItemOutput',

  props: ['output'],

  data() {
    return {
      volume: this.output.selected ? this.output.volume : 0,
      cursor: mdiCancel
    }
  },

  computed: {
    type_class() {
      if (this.output.type.startsWith('AirPlay')) {
        return 'cast-variant'
      } else if (this.output.type === 'Chromecast') {
        return 'cast'
      } else if (this.output.type === 'fifo') {
        return 'pipe'
      } else {
        return 'server'
      }
    }
  },

  watch: {
    output() {
      this.volume = this.output.volume
    }
  },

  methods: {
    change_volume() {
      webapi.player_output_volume(this.output.id, this.volume)
    },

    set_enabled() {
      const values = {
        selected: !this.output.selected
      }
      webapi.output_update(this.output.id, values)
    }
  }
}
</script>

<style></style>
