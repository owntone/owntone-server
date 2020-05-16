module.exports = {
  // Runtime compiler is required to compile vue templates
  runtimeCompiler: true,

  // Output path for the generated static assets (js/css)
  outputDir: 'dist',

  // Output path for the generated index.html
  indexPath: 'index.html',

  assetsDir: 'player',

  // Do not add hashes to the generated js/css filenames, would otherwise
  // require to adjust the Makefile in htdocs each time the web interface is
  // build
  filenameHashing: false,

  css: {
    sourceMap: true
  },

  devServer: {
    // Proxy forked-daapd JSON API calls to the forked-daapd server running on
    // localhost:3689
    proxy: {
      '/api': {
        target: process.env.VUE_APP_JSON_API_SERVER
      },
      '/artwork': {
        target: process.env.VUE_APP_JSON_API_SERVER
      },
      '/stream.mp3': {
        target: process.env.VUE_APP_JSON_API_SERVER
      }
    }
  }
}
