<template>
  <section class="section" :class="{ 'pt-0': $slots.options }">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div v-if="$slots.options" class="my-2">
            <div :class="{ 'is-hidden': hidden }" class="mt-4">
              <slot name="options" />
            </div>
            <div class="buttons is-centered">
              <button class="button is-small" @click="hidden = !hidden">
                <mdicon class="icon" :name="icon" size="16" />
              </button>
            </div>
          </div>
          <div>
            <nav class="level is-clipped">
              <div class="level-left">
                <div class="level-item has-text-centered-mobile">
                  <div>
                    <slot name="heading" />
                  </div>
                </div>
              </div>
              <div
                v-if="$slots.actions"
                class="level-right is-flex-shrink-1 has-text-centered-mobile"
              >
                <div class="buttons">
                  <slot name="actions" />
                </div>
              </div>
            </nav>
            <slot name="content" />
            <div
              v-if="$slots.footer"
              class="is-flex is-justify-content-center mt-4"
            >
              <slot name="footer" />
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
export default {
  name: 'ContentWithHeading',
  data() {
    return {
      hidden: true
    }
  },
  computed: {
    icon() {
      return this.hidden ? 'chevron-down' : 'chevron-up'
    }
  }
}
</script>
