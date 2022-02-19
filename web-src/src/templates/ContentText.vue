<template>
  <section class="section fd-content py-3">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <slot name="content" />
        </div>
      </div>
    </div>
  </section>
</template>

<script>
export default {
  name: 'ContentText'
}
</script>

<style></style>
