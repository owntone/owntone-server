<template>
  <section class="section pb-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <form @submit.prevent="$emit('search')">
            <div class="field">
              <div class="control has-icons-left">
                <input
                  v-model="searchStore.query"
                  class="input is-rounded"
                  type="search"
                  :placeholder="$t('page.search.placeholder')"
                  autocomplete="off"
                />
                <mdicon class="icon is-left" name="magnify" size="16" />
              </div>
              <slot name="help" />
            </div>
          </form>
          <div class="field is-grouped is-grouped-multiline mt-4">
            <div v-for="item in history" :key="item" class="control">
              <div class="tags has-addons">
                <a
                  class="tag"
                  @click="$emit('search-query', item)"
                  v-text="item"
                />
                <a class="tag is-delete" @click="searchStore.remove(item)" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <tabs-search
    @search-library="$emit('search-library')"
    @search-spotify="$emit('search-spotify')"
  />
  <content-with-heading v-for="[type, items] in results" :key="type">
    <template #heading>
      <pane-title :content="{ title: $t(`page.search.${type}s`) }" />
    </template>
    <template #content>
      <component :is="components[type]" :items="getItems(items)" :load="load" />
    </template>
    <template v-if="!expanded" #footer>
      <control-button
        v-if="items.total"
        :button="{
          handler: () => $emit('expand', type),
          title: $t(
            `page.search.show-${type}s`,
            { count: $n(items.total) },
            items.total
          )
        }"
      />
      <div v-else class="has-text-centered-mobile">
        <i v-text="$t('page.search.no-results')" />
      </div>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsSearch from '@/components/TabsSearch.vue'
import { useSearchStore } from '@/stores/search'

export default {
  name: 'ContentWithSearch',
  components: { ContentWithHeading, ControlButton, PaneTitle, TabsSearch },
  props: {
    components: { default: null, type: Object },
    expanded: { default: false, type: Boolean },
    getItems: { default: null, type: Function },
    history: { default: null, type: Array },
    load: { default: null, type: Function },
    results: { default: null, type: Object }
  },
  emits: [
    'expand',
    'search',
    'search-library',
    'search-query',
    'search-spotify'
  ],
  setup() {
    return {
      searchStore: useSearchStore()
    }
  }
}
</script>
