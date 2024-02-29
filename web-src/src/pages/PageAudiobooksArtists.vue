<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="artists.indices" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.audiobooks.artists.title')" />
        <p
          class="heading"
          v-text="$t('page.audiobooks.artists.count', { count: artists.count })"
        />
      </template>
      <template #heading-right />
      <template #content>
        <list-artists :artists="artists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { GroupedList, byName } from '@/lib/GroupedList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_artists('audiobook')
  },

  set(vm, response) {
    vm.artists_list = new GroupedList(response.data)
  }
}

export default {
  name: 'PageAudiobooksArtists',
  components: {
    ContentWithHeading,
    TabsAudiobooks,
    IndexButtonList,
    ListArtists
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      artists_list: new GroupedList()
    }
  },

  computed: {
    artists() {
      if (!this.artists_list) {
        return []
      }
      this.artists_list.group(byName('name_sort', true))
      return this.artists_list
    }
  }
}
</script>

<style></style>
