// The Vue build version to load with the `import` command
// (runtime-only or standalone) has been set in webpack.base.conf with an alias.
import Vue from 'vue'
import App from './App'
import { router } from './router'
import store from './store'
import './filter'
import './progress'
import vClickOutside from 'v-click-outside'
import 'bulma/css/bulma.css'
import 'mdi/css/materialdesignicons.css'
import 'vue-range-slider/dist/vue-range-slider.css'
import './mystyles.css'

Vue.config.productionTip = false

Vue.use(vClickOutside)

/* eslint-disable no-new */
new Vue({
  el: '#app',
  router,
  store,
  components: { App },
  template: '<App/>'
})
