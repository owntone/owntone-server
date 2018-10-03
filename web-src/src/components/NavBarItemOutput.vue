<template>
  <div class="navbar-item">
    <div class="level is-mobile">
      <div class="level-left fd-expanded">
        <div class="level-item" style="flex-grow: 0;">
          <span class="icon fd-has-action" :class="{ 'has-text-grey-light': !output.selected }" v-on:click="set_enabled"><i class="mdi mdi-18px" v-bind:class="type_class"></i></span>
        </div>
        <div class="level-item fd-expanded">
          <div class="fd-expanded">
            <p class="heading" :class="{ 'has-text-grey-light': !output.selected }">{{ output.name }}</p>
            <range-slider
              class="slider fd-has-action"
              min="0"
              max="100"
              step="1"
              :disabled="!output.selected"
              :value="volume"
              @change="set_volume" >
            </range-slider>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import RangeSlider from 'vue-range-slider'
import webapi from '@/webapi'

export default {
  name: 'NavBarItemOutput',
  components: { RangeSlider },

  props: [ 'output' ],

  computed: {
    type_class () {
      if (this.output.type === 'AirPlay') {
        return 'mdi-airplay'
      } else if (this.output.type === 'fifo') {
        return 'mdi-pipe'
      } else {
        return 'mdi-server'
      }
    },

    volume () {
      return this.output.selected ? this.output.volume : 0
    }
  },

  methods: {
    play_next: function () {
      webapi.player_next()
    },

    set_volume: function (newVolume) {
      webapi.player_output_volume(this.output.id, newVolume)
    },

    set_enabled: function () {
      const values = {
        'selected': !this.output.selected
      }
      webapi.output_update(this.output.id, values)
    }
  }
}
</script>

<style>
</style>
