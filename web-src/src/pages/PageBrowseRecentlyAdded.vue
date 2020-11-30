<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">{{ recently_added }} albums</p>
      </template>
    </content-with-heading>

    <content-with-heading v-if="show_recent_today && recently_added_today.items.length">
      <template slot="heading-left">
        <p class="title is-6">Today</p>
        <p class="heading">{{ recently_added_today.items.length }} albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added_today.items"></list-albums>
      </template>
    </content-with-heading>

    <content-with-heading v-if="show_recent_week && recently_added_week.items.length">
      <template slot="heading-left">
        <p class="title is-6">This Week</p>
        <p class="heading">{{ recently_added_week.items.length }} albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added_week.items"></list-albums>
      </template>
    </content-with-heading>

    <content-with-heading v-if="show_recent_month && recently_added_month.items.length">
      <template slot="heading-left">
        <p class="title is-6">This Month </p>
        <p class="heading">{{ recently_added_month.items.length }} albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added_month.items"></list-albums>
      </template>
    </content-with-heading>

    <content-with-heading v-if="show_recent_older && recently_added_older.items.length">
      <template slot="heading-left">
        <p class="title is-6">Older</p>
        <p class="heading">{{ recently_added_older.items.length }} albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added_older.items"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListAlbums from '@/components/ListAlbums'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'album',
      expression: 'time_added after today and media_kind is music order by time_added desc',
      limit: 100
    })
  },

  set: function (vm, response) {
    vm.recently_added_today = response.data.albums
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [LoadDataBeforeEnterMixin(browseData)],
  components: { ContentWithHeading, TabsMusic, ListAlbums },

  data () {
    return {
      recently_added_today: { items: [] },
      recently_added_week: { items: [] },
      recently_added_month: { items: [] },
      recently_added_older: { items: [] },

      limit: 100
    }
  },

  created () {
    if (this.recently_added < this.limit) {
      webapi.search({
        type: 'album',
        expression: 'time_added after this week and media_kind is music order by time_added desc',
        limit: this.limit - this.recently_added
      }).then(({ data }) => {
        this.recently_added_week = data.albums

        if (this.recently_added < this.limit) {
          webapi.search({
            type: 'album',
            expression: 'time_added after last month and media_kind is music order by time_added desc',
            limit: this.limit - this.recently_added
          }).then(({ data }) => {
            this.recently_added_month = data.albums

            if (this.recently_added < this.limit) {
              webapi.search({
                type: 'album',
                expression: 'time_added before last month and media_kind is music order by time_added desc',
                limit: this.limit - this.recently_added
              }).then(({ data }) => {
                this.recently_added_older = data.albums
              })
            }
          })
        }
      })
    }
  },

  computed: {
    show_recent_today () {
      return this.recently_added_today.items.length > 0
    },
    show_recent_week () {
      return this.recently_added_week.items.length > 0
    },
    show_recent_month () {
      return this.recently_added_month.items.length > 0
    },
    show_recent_older () {
      return this.recently_added_older.items.length > 0
    },
    recently_added () {
      return this.recently_added_today.items.length + this.recently_added_week.items.length + this.recently_added_month.items.length + this.recently_added_older.items.length
    }
  }
}
</script>

<style>
</style>
