<template>
  <section class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="columns is-flex-direction-row-reverse">
            <div class="column is-flex has-image">
              <slot name="image" />
            </div>
            <div class="column m-auto is-three-fifths has-text-centered-mobile">
              <slot name="heading" />
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <section class="section pt-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <slot name="content" />
          <slot name="footer" />
        </div>
      </div>
    </div>
  </section>
</template>

<script>
export default {
  name: 'ContentWithHero'
}
</script>

<style lang="scss" scoped>
@use 'bulma/sass/utilities/mixins';

.has-image {
  @include mixins.mobile {
    justify-content: center;
  }
  @include mixins.tablet {
    justify-content: right;
  }
}
</style>
