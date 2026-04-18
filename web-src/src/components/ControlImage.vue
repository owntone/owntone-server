<template>
  <figure class="figure has-shadow is-clickable">
    <img v-lazy="source" @click="$emit('click')" />
  </figure>
</template>

<script setup>
import { computed } from 'vue'
import { renderSVG } from '@/lib/SVGRenderer'

const props = defineProps({
  caption: { type: String, default: '' },
  url: { type: String, default: '' }
})

defineEmits(['click'])

const uri = computed(() =>
  renderSVG({
    alternate: props.caption,
    caption: props.caption.substring(0, 2),
    font: { family: 'sans-serif', weight: 'bold' },
    size: 600
  })
)

const source = computed(() => ({
  lifecycle: {
    error: (el) => {
      el.src = uri.value
    }
  },
  src: props.url || uri.value
}))
</script>

<style lang="scss" scoped>
@use 'bulma/sass/utilities/mixins';

.figure {
  align-items: center;
  display: flex;
  justify-content: center;
  &.is-small {
    width: 4rem;
    height: 4rem;
    img {
      border-radius: var(--bulma-radius-small);
      max-width: 4rem;
      max-height: 4rem;
    }
  }
  &.is-medium {
    @include mixins.tablet {
      justify-content: right;
    }
    img {
      border-radius: var(--bulma-radius);
      max-height: calc(150px - 1.5rem);
    }
  }
  &.is-normal {
    img {
      border-radius: var(--bulma-radius-large);
      width: 100%;
    }
  }
  &.is-big {
    @include mixins.mobile {
      @media screen and (orientation: landscape) {
        img {
          display: none;
        }
      }
    }
    img {
      border-radius: var(--bulma-radius-large);
      max-height: calc(100vh - 26rem);
    }
    &.is-masked {
      filter: blur(0.5rem) opacity(0.2);
    }
  }
}
.has-shadow img {
  box-shadow:
    0 0.25rem 0.5rem 0 var(--bulma-background-active),
    0 0.375rem 1.25rem 0 var(--bulma-background-active);
}
</style>
