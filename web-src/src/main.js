import './mystyles.scss'
import App from './App.vue'
import VueClickAway from 'vue3-click-away'
import VueLazyLoad from 'vue3-lazyload'
import VueProgressBar from '@aacassandra/vue3-progressbar'
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { filters } from './filter'
import i18n from './i18n'
import { icons } from './icons'
import mdiVue from 'mdi-vue/v3'
import { router } from './router'

const app = createApp(App)
  .use(createPinia())
  .use(router)
  .use(VueProgressBar)
  .use(VueClickAway)
  .use(VueLazyLoad, { log: false })
  .use(mdiVue, { icons })
  .use(i18n)

app.config.globalProperties.$filters = filters
app.mount('#app')
