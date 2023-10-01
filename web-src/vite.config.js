import path from 'path'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueI18n from '@intlify/unplugin-vue-i18n/vite'

// Support for setting the OwnTone server URL as env var VITE_OWNTONE_URL
// in development mode.
// E. g. start the DEV server with
//
//     VITE_OWNTONE_URL=https://owntone.local:3689; npm run serve
//
// will connect the web interface with a remote OwnTone server.
const owntoneUrl = process.env.VITE_OWNTONE_URL ?? 'http://localhost:3689'

// https://vitejs.dev/config/
export default defineConfig({
  resolve: { alias: { '@': '/src' } },
  plugins: [
    vue(),
    vueI18n({
      include: path.resolve(__dirname, './src/locales/**')
    })
  ],
  pluginOptions: {
    i18n: {
      locale: 'en',
      fallbackLocale: 'en',
      localeDir: 'locales',
      enableLegacy: false,
      runtimeOnly: false,
      compositionOnly: false,
      fullInstall: true
    }
  },
  build: {
    outDir: '../htdocs',
    rollupOptions: {
      output: {
        entryFileNames: `assets/[name].js`,
        chunkFileNames: `assets/[name].js`,
        assetFileNames: `assets/[name].[ext]`
      }
    }
  },
  server: {
    proxy: {
      '/api': {
        target: owntoneUrl
      },
      '/artwork': {
        target: owntoneUrl
      },
      '/stream.mp3': {
        target: owntoneUrl
      }
    }
  }
})
