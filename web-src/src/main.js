import { createApp } from 'vue'
import store from './store'
import { router } from './router'
import VueProgressBar from '@aacassandra/vue3-progressbar'
import VueClickAway from 'vue3-click-away'
import VueLazyLoad from 'vue3-lazyload'
import VueScrollTo from 'vue-scrollto'
import mdiVue from 'mdi-vue/v3'
import { filters } from './filter'
import { icons } from './icons'
import App from './App.vue'

import './mystyles.scss'
import '@vueform/slider/themes/default.css'

const app = createApp(App)
  .use(store)
  .use(router)
  .use(VueProgressBar, {
    color: 'hsl(204, 86%, 53%)',
    failedColor: 'red',
    height: '1px'
  })
  .use(VueClickAway)
  .use(VueLazyLoad, {
    // Do not log errors, if image does not exist
    log: false
  })
  .use(VueScrollTo)
  .use(mdiVue, {
    icons: icons
  })

app.config.globalProperties.$filters = filters
app.mount('#app')
