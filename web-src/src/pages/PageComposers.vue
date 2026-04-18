<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="composers.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-composers :items="composers" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListComposers from '@/components/ListComposers.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'

const composers = ref(new GroupedList())
const { t } = useI18n()

const heading = computed(() => ({
  subtitle: [{ count: composers.value.total, key: 'data.composers' }],
  title: t('page.composers.title')
}))

onMounted(async () => {
  const composerList = await library.composers('music')
  composers.value = new GroupedList(composerList, {
    index: { field: 'name_sort', type: String }
  })
})
</script>
