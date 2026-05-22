import './styles.scss'
import App from './App.vue'
import VueLazyLoad from 'vue3-lazyload'
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import i18n from './i18n'
import { icons } from './icons'
import mdiVue from 'mdi-vue/v3'
import { router } from './router'

const app = createApp(App)
  .use(createPinia())
  .use(router)
  .use(VueLazyLoad, { log: false })
  .use(mdiVue, { icons })
  .use(i18n)

app.mount('#app')
