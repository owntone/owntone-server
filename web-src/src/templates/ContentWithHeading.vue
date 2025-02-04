<template>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <section v-if="$slots.options" ref="options">
            <div :class="{ 'is-hidden': hidden }">
              <slot name="options" />
            </div>
            <div class="buttons is-centered">
              <button class="button is-small" @click="hidden = !hidden">
                <mdicon class="icon" :name="icon" size="16" />
              </button>
            </div>
          </section>
          <div>
            <nav class="level is-clipped">
              <div class="level-left is-flex-shrink-1">
                <div
                  class="level-item is-flex-shrink-1 has-text-centered-mobile"
                >
                  <div>
                    <slot name="heading-left" />
                  </div>
                </div>
              </div>
              <div class="level-right has-text-centered-mobile">
                <slot name="heading-right" />
              </div>
            </nav>
            <slot name="content" />
            <div class="mt-4">
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
