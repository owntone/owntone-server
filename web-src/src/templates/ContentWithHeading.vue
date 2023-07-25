<template>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <section v-if="$slots.options">
            <div ref="options_ref" style="height: 1px" />
            <slot name="options" />
            <nav class="buttons is-centered mt-4 mb-2">
              <router-link class="button is-small is-white" :to="position"
                ><mdicon class="icon is-small" :name="icon_name" size="16"
              /></router-link>
            </nav>
          </section>
          <div :class="{ 'fd-content-with-option': $slots.options }">
            <nav id="top" class="level is-clipped">
              <!-- Left side -->
              <div class="level-left is-flex-shrink-1">
                <div
                  class="level-item is-flex-shrink-1 has-text-centered-mobile"
                >
                  <div>
                    <slot name="heading-left" />
                  </div>
                </div>
              </div>
              <!-- Right side -->
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
    icon_name() {
      return this.options_visible ? 'chevron-up' : 'chevron-down'
    },
    position() {
      return { hash: this.options_visible ? '#top' : '#app' }
    }
  },
  mounted() {
    if (this.$slots.options) {
      this.observer = new IntersectionObserver(this.onElementObserved, {
        rootMargin: '-82px 0px 0px 0px',
        threshold: 1.0
      })
      this.observer.observe(this.$refs.options_ref)
    }
  },
  methods: {
    onElementObserved(entries) {
      entries.forEach(({ target, isIntersecting }) => {
        this.options_visible = isIntersecting
      })
    },
    visibilityChanged(isVisible) {
      this.options_visible = isVisible
    }
  }
}
</script>

<style></style>
