<template>
  <div v-if="!isItem" class="py-5">
    <div class="media-content">
      <span
        :id="`index_${index}`"
        class="tag is-small has-text-weight-bold"
        v-text="index"
      />
    </div>
  </div>
  <div
    v-else
    class="media is-align-items-center is-clickable mb-0"
    :class="{ 'with-progress': progress }"
    @click="open"
  >
    <mdicon v-if="icon" class="media-left icon" :name="icon" />
    <control-image v-if="image" :url="image" class="media-left is-small" />
    <div class="media-content">
      <div
        v-for="(line, position) in lines"
        :key="position"
        :class="{
          'is-size-6': position === 0,
          'is-size-7': position !== 0,
          'has-text-weight-bold': position !== 2,
          'has-text-grey': position !== 0
        }"
        v-text="line"
      />
      <progress
        v-if="progress"
        class="progress is-dark"
        max="1"
        :value="progress"
      />
    </div>
    <div class="media-right">
      <a @click.prevent.stop="openDetails">
        <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
      </a>
    </div>
  </div>
</template>

<script>
import ControlImage from '@/components/ControlImage.vue'

export default {
  name: 'ListItem',
  components: { ControlImage },
  props: {
    icon: { default: null, type: String },
    image: { default: null, type: String },
    index: { default: null, type: [String, Number] },
    isItem: { default: true, type: Boolean },
    lines: { default: null, type: Array },
    progress: { default: null, type: Number }
  },
  emits: ['open', 'openDetails'],
  methods: {
    open() {
      this.$emit('open')
    },
    openDetails() {
      this.$emit('openDetails')
    }
  }
}
</script>

<style scoped>
.progress {
  height: 0.25rem;
}

.media.with-progress {
  margin-top: 0.375rem;
}
</style>
