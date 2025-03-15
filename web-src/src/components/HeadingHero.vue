<template>
  <div class="title is-5" v-text="content.title" />
  <div class="subtitle is-6">
    <a @click="content.handler" v-text="content.subtitle" />
  </div>
  <div
    class="is-size-7 is-uppercase has-text-centered-mobile"
    v-text="content.count"
  />
  <div class="buttons is-centered-mobile mt-5">
    <control-button
      v-for="(action, index) in content.actions"
      :key="index"
      :button="action"
    />
  </div>
</template>

<script>
import ControlButton from '@/components/ControlButton.vue'

export default {
  name: 'HeadingHero',
  components: { ControlButton },
  props: { content: { required: true, type: Object } }
}
</script>
