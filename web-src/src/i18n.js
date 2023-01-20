import { createI18n } from 'vue-i18n'

/*
 * All i18n resources specified in the plugin `include` option can be loaded
 * at once using the import syntax.
 */
import messages from '@intlify/vite-plugin-vue-i18n/messages'

export default createI18n({
  legacy: false,
  globalInjection: true,
  availableLocales: ('de','en','fr'),
  locale: navigator.language,
  fallbackLocale: 'en',
  fallbackWarn: false,
  missingWarn: false,
  messages
})
