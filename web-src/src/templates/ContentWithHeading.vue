<template>
  <section class="section fd-content">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <section v-if="$slots['options']">
            <slot name="options"></slot>
            <nav class="buttons is-centered" style="margin-bottom: 6px; margin-top: 16px;">
              <a class="button is-small is-white" @click="scroll_to_top"><span class="icon is-small"><i class="mdi mdi-chevron-up"></i></span></a>
            </nav>
          </section>
          <nav class="level" id="top">
            <!-- Left side -->
            <div class="level-left">
              <div class="level-item has-text-centered-mobile">
                <div>
                  <slot name="heading-left"></slot>
                </div>
              </div>
            </div>

            <!-- Right side -->
            <div class="level-right has-text-centered-mobile">
              <slot name="heading-right"></slot>
            </div>
          </nav>
          <slot name="content"></slot>
          <div style="margin-top: 16px;">
            <slot name="footer"></slot>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
export default {
  name: 'ContentWithHeading',

  methods: {
    scroll_to_top: function () {
      window.scrollTo({ top: 0, behavior: 'smooth' })
    }
  }
}
</script>

<style>
</style>
