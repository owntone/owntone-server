<template>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <section v-if="$slots.options" ref="options">
            <slot name="options" />
            <nav class="buttons is-centered">
              <router-link class="button is-small" :to="position">
                <mdicon class="icon" :name="icon" size="16" />
              </router-link>
            </nav>
          </section>
          <div>
            <nav id="top" class="level is-clipped">
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
      options_visible: false
    }
  },
  computed: {
    icon() {
      return this.options_visible ? 'chevron-up' : 'chevron-down'
    },
    position() {
      return {
        hash: this.options_visible ? '#top' : '#app',
        query: this.$route.query
      }
    }
  },
  mounted() {
    if (this.$slots.options) {
      this.observer = new IntersectionObserver(this.onElementObserved, {
        rootMargin: '-82px 0px 0px 0px',
        threshold: 1.0
      })
      this.observer.observe(this.$refs.options)
    }
  },
  methods: {
    onElementObserved(entries) {
      entries.forEach(({ isIntersecting }) => {
        this.options_visible = isIntersecting
      })
    },
    visibilityChanged(isVisible) {
      this.options_visible = isVisible
    }
  }
}
</script>
