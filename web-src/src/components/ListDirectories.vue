<template>
  <div v-if="$route.query.directory" class="media is-align-items-center mb-0">
    <mdicon
      class="icon media-left is-clickable"
      name="chevron-left"
      @click="openParent"
    />
    <div class="media-content">
      <nav class="breadcrumb">
        <ul>
          <li v-for="directory in directories" :key="directory.index">
            <a @click="open(directory)" v-text="directory.name" />
          </li>
        </ul>
      </nav>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
  <list-item
    v-for="item in items"
    :key="item.path"
    icon="folder"
    :lines="[item.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <modal-dialog-directory
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ListItem from '@/components/ListItem.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'

defineProps({ items: { required: true, type: Array } })

const route = useRoute()
const router = useRouter()

const selectedItem = ref('')
const showDetailsModal = ref(false)

const directories = computed(() => {
  const items = []
  let path = ''
  route.query?.directory
    ?.split('/')
    .slice(1, -1)
    .forEach((name, index) => {
      path = `${path}/${name}`
      items.push({ index, name, path })
    })
  return items
})

const open = (item) => {
  const nextRoute = { name: 'files' }
  nextRoute.query = item.index !== 0 && { directory: item.path }
  router.push(nextRoute)
}

const openDetails = (item) => {
  selectedItem.value = item.path
  showDetailsModal.value = true
}

const openParent = () => {
  const parent = directories.value.slice(-1).pop()
  if (parent) {
    open(parent)
  }
}
</script>
