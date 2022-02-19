<template>
  <div :id="'index_' + composer.name.charAt(0).toUpperCase()" class="media">
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6">
        {{ composer.name }}
      </h1>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemComposer',
  props: ['composer']
}
</script>

<style></style>
