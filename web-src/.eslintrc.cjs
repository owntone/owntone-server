module.exports = {
  env: {
    node: true
  },
  extends: ['eslint:recommended', 'plugin:vue/vue3-recommended', 'prettier'],
  rules: {
    // Override/add rules settings here, such as:
    'no-unused-vars': ['error', { args: 'none' }],
    'vue/prop-name-casing': ['warn', 'snake_case']
  }
}
