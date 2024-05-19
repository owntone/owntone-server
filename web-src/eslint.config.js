import eslintConfigPrettier from 'eslint-config-prettier'
import globals from 'globals'
import js from '@eslint/js'
import pluginVue from 'eslint-plugin-vue'

export default [
  {
    files: ['src/**/*.js', 'src/**/.vue'],
    languageOptions: {
      globals: {
        ...globals.node
      }
    }
  },
  eslintConfigPrettier,
  js.configs.all,
  ...pluginVue.configs['flat/recommended'],
  {
    rules: {
      camelcase: 'off',
      'consistent-this': 'off',
      'id-length': 'off',
      'max-lines': 'off',
      'max-lines-per-function': 'off',
      'max-statements': 'off',
      'no-bitwise': 'off',
      'no-magic-numbers': 'off',
      'no-nested-ternary': 'off',
      'no-plusplus': 'off',
      'no-ternary': 'off',
      'no-undef': 'off',
      'no-unused-vars': ['error', { args: 'none', caughtErrors: 'none' }],
      'one-var': 'off',
      'sort-keys': 'off',
      'vue/html-self-closing': 'off',
      'vue/max-attributes-per-line': 'off',
      'vue/prop-name-casing': 'off'
    }
  }
]
