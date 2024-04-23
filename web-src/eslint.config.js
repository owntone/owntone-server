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
      'default-param-last': 'off',
      'id-length': 'off',
      'max-lines': 'off',
      'max-lines-per-function': 'off',
      'max-statements': 'off',
      'no-bitwise': 'off',
      'no-magic-numbers': 'off',
      'no-negated-condition': 'off',
      'no-nested-ternary': 'off',
      'no-plusplus': 'off',
      'no-shadow': 'off',
      'no-ternary': 'off',
      'no-undef': 'off',
      'no-undefined': 'off',
      'no-unused-expressions': 'off',
      'no-unused-vars': ['error', { args: 'none', caughtErrors: 'none' }],
      'no-useless-assignment': 'off',
      'one-var': 'off',
      'prefer-destructuring': 'off',
      'prefer-named-capture-group': 'off',
      'sort-keys': 'off',
      'vue/html-self-closing': 'off',
      'vue/max-attributes-per-line': 'off',
      'vue/prop-name-casing': 'off',
      'vue/singleline-html-element-content-newline': 'off'
    }
  }
]
