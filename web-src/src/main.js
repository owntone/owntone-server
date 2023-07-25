import './mystyles.scss'
import App from './App.vue'
import { createApp } from 'vue'
import { filters } from './filter'
import i18n from './i18n'
import { icons } from './icons'
import mdiVue from 'mdi-vue/v3'
import VueClickAway from 'vue3-click-away'
import VueLazyLoad from 'vue3-lazyload'
import VueProgressBar from '@aacassandra/vue3-progressbar'
import { router } from './router'
import store from './store'

const app = createApp(App)
  .use(store)
  .use(router)
  .use(VueProgressBar)
  .use(VueClickAway)
  .use(VueLazyLoad, {
    // Do not log errors, if image does not exist
    log: false
  })
  .use(mdiVue, {
    icons
  })
  .use(i18n)

app.config.globalProperties.$filters = filters
app.mount('#app')
