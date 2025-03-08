import { createI18n } from 'vue-i18n'

import messages from '@intlify/unplugin-vue-i18n/messages'

export default createI18n({
  availableLocales: Object.keys(messages),
  fallbackLocale: 'en',
  fallbackWarn: false,
  legacy: false,
  locale: navigator.language,
  messages,
  missingWarn: false
})
