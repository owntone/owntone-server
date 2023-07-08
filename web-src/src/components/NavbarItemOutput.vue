<template>
  <div class="navbar-item">
    <div class="level is-mobile">
      <div class="level-left is-flex-grow-1">
        <div class="level-item is-flex-grow-0">
          <a
            class="button is-clickable is-white is-small"
            :class="{ 'has-text-grey-light': !output.selected }"
            @click="set_enabled"
          >
            <mdicon
              class="icon"
              :name="type_class"
              size="18"
              :title="output.type"
            />
          </a>
        </div>
        <div class="level-item">
          <div class="is-flex-grow-1">
            <p
              class="heading"
              :class="{ 'has-text-grey-light': !output.selected }"
              v-text="output.name"
            />
            <control-slider
              v-model:value="volume"
              :disabled="!output.selected"
              :max="100"
              :cursor="cursor"
              @change="change_volume"
            />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import { mdiCancel } from '@mdi/js'
import webapi from '@/webapi'

export default {
  name: 'NavbarItemOutput',
  components: {
    ControlSlider
  },
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
