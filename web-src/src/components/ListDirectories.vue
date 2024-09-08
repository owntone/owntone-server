<template>
  <div v-if="$route.query.directory" class="media is-align-items-center">
    <mdicon
      class="icon media-left is-clickable"
      name="chevron-left"
      @click="open_parent"
    />
    <div class="media-content">
      <nav class="breadcrumb">
        <ul>
          <li v-for="directory in directories" :key="directory.index">
            <a @click="open(directory)">
              <span v-text="directory.name" />
            </a>
          </li>
        </ul>
      </nav>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
  <template v-for="item in items" :key="item.path">
    <div class="media is-align-items-center" @click="open(item)">
      <mdicon class="media-left is-clickable icon" name="folder" />
      <div class="media-content is-clickable is-clipped">
        <p class="title is-6" v-text="item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-directory
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'

export default {
  name: 'ListDirectories',
  components: { ModalDialogDirectory },
  props: { items: { required: true, type: Array } },

  data() {
    return {
      selected_item: '',
      show_details_modal: false
    }
  },

  computed: {
    directories() {
      const directories = []
      let path = ''
      this.$route.query?.directory
        .split('/')
        .slice(1, -1)
        .forEach((name, index) => {
          path = `${path}/${name}`
          directories.push({ index, name, path })
        })
      return directories
    }
  },

  methods: {
    open(item) {
      const route = { name: 'files' }
      if (item.index !== 0) {
        route.query = { directory: item.path }
      }
      this.$router.push(route)
    },
    open_dialog(item) {
      this.selected_item = item.path
      this.show_details_modal = true
    },
    open_parent() {
      this.open(this.directories.slice(-1).pop())
    }
  }
}
</script>
