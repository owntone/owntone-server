import eslintConfigPrettier from 'eslint-config-prettier'
import globals from 'globals'
import js from '@eslint/js'
import pluginVue from 'eslint-plugin-vue'

export default [
  ...pluginVue.configs['flat/recommended'],
  {
    files: ['**/*.{js,vue}'],
    languageOptions: { globals: { ...globals.browser, ...globals.node } },
    rules: {
      ...eslintConfigPrettier.rules,
      ...js.configs.all.rules,
      camelcase: 'off',
      'id-length': 'off',
      'max-lines-per-function': 'off',
      'no-bitwise': 'off',
      'no-magic-numbers': 'off',
      'one-var': 'off',
      'sort-keys': 'off'
    }
  }
]
