<template>
  <div class="title is-4">
    <a v-if="item.handler" @click="item.handler" v-text="item.name"></a>
    <span v-else v-text="item.name" />
  </div>
  <control-image
    v-if="item.image"
    :url="item.image"
    :artist="item.artist"
    :album="item.name"
    class="is-normal mb-5"
  />
  <slot v-if="$slots.buttons" name="buttons" />
  <div
    v-for="property in item.properties?.filter((p) => p.value)"
    :key="property.label"
    class="mb-3"
  >
    <div class="is-size-7 is-uppercase" v-text="$t(property.label)" />
    <div class="title is-6">
      <a
        v-if="property.handler"
        @click="property.handler"
        v-text="property.value"
      />
      <span v-else class="title is-6" v-text="property.value" />
    </div>
  </div>
</template>

<script>
import ControlImage from '@/components/ControlImage.vue'

export default {
  name: 'ListProperties',
  components: { ControlImage },
  props: {
    item: { required: true, type: Object }
  }
}
</script>
