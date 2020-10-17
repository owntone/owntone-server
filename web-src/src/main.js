// The Vue build version to load with the `import` command
// (runtime-only or standalone) has been set in webpack.base.conf with an alias.
import Vue from 'vue'
import App from './App'
import { router } from './router'
import store from './store'
import './filter'
import './progress'
import vClickOutside from 'v-click-outside'
import VueTinyLazyloadImg from 'vue-tiny-lazyload-img'
import VueObserveVisibility from 'vue-observe-visibility'
import VueScrollTo from 'vue-scrollto'
import 'mdi/css/materialdesignicons.css'
import 'vue-range-slider/dist/vue-range-slider.css'
import './mystyles.scss'

Vue.config.productionTip = false

Vue.use(vClickOutside)
Vue.use(VueTinyLazyloadImg)
Vue.use(VueObserveVisibility)
Vue.use(VueScrollTo)

/* eslint-disable no-new */
new Vue({
  el: '#app',
  router,
  store,
  components: { App },
  template: '<App/>'
})
