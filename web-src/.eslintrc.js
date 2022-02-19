module.exports = {
  env: {
    node: true
  },
  extends: ['eslint:recommended', 'plugin:vue/vue3-recommended', 'prettier'],
  rules: {
    // override/add rules settings here, such as:
    // 'vue/no-unused-vars': 'error'
    'no-unused-vars': ['error', { args: 'none' }],
    'vue/require-prop-types': 'off',
    'vue/require-default-prop': 'off',
    'vue/prop-name-casing': ['warn', 'snake_case']
  }
}
