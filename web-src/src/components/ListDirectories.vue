<template>
  <div
    v-if="$route.query.directory"
    class="media is-align-items-center"
    @click="open_parent_directory()"
  >
    <figure class="media-left is-clickable">
      <mdicon class="icon" name="subdirectory-arrow-left" size="16" />
    </figure>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6">..</h1>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
  <template v-for="directory in directories" :key="directory.path">
    <div class="media is-align-items-center" @click="open_directory(directory)">
      <figure class="media-left is-clickable">
        <mdicon class="icon" name="folder" size="16" />
      </figure>
      <div class="media-content is-clickable is-clipped">
        <h1
          class="title is-6"
          v-text="directory.path.substring(directory.path.lastIndexOf('/') + 1)"
        />
        <h2 class="subtitle is-7 has-text-grey-light" v-text="directory.path" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(directory)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-directory
      :show="show_details_modal"
      :directory="selected_directory"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'

export default {
  name: 'ListDirectories',
  components: { ModalDialogDirectory },

  props: ['directories'],

  data() {
    return {
      show_details_modal: false,
      selected_directory: ''
    }
  },

  computed: {
    current_directory() {
      if (this.$route.query && this.$route.query.directory) {
        return this.$route.query.directory
      }
      return '/'
    }
  },

  methods: {
    open_parent_directory() {
      const parent = this.current_directory.slice(
        0,
        this.current_directory.lastIndexOf('/')
      )
      if (
        parent === '' ||
        this.$store.state.config.directories.includes(this.current_directory)
      ) {
        this.$router.push({ name: 'files' })
      } else {
        this.$router.push({
          name: 'files',
          query: {
            directory: this.current_directory.slice(
              0,
              this.current_directory.lastIndexOf('/')
            )
          }
        })
      }
    },

    open_directory(directory) {
      this.$router.push({
        name: 'files',
        query: { directory: directory.path }
      })
    },

    open_dialog(directory) {
      this.selected_directory = directory.path
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
