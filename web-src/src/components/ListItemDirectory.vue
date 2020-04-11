<template functional>
  <div class="media">
    <figure class="media-left fd-has-action" @click="listeners.click">
      <span class="icon">
        <i class="mdi mdi-folder"></i>
      </span>
    </figure>
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.directory.path.substring(props.directory.path.lastIndexOf('/') + 1) }}</h1>
      <h2 class="subtitle is-7 has-text-grey-light">{{ props.directory.path }}</h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemDirectory',
  props: ['directory']
}
</script>

<style>
</style>
