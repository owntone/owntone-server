import path from 'path'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import i18n from '@intlify/unplugin-vue-i18n/vite'

/*
 * In development mode, use the VITE_OWNTONE_URL environment variable to set
 * the remote OwnTone server URL. For example:
 *
 * export VITE_OWNTONE_URL=http://owntone.local:3689; npm run serve
 */
const owntoneUrl = process.env.VITE_OWNTONE_URL ?? 'http://localhost:3689'

export default defineConfig({
  resolve: { alias: { '@': '/src' } },
  plugins: [
    vue(),
    i18n({
      include: path.resolve(__dirname, './src/i18n/**.json')
    })
  ],
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
