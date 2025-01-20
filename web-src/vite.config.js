import { defineConfig } from 'vite'
import i18n from '@intlify/unplugin-vue-i18n/vite'
import path from 'path'
import vue from '@vitejs/plugin-vue'

/*
 * In development mode, use the VITE_OWNTONE_URL environment variable to set
 * the remote OwnTone server URL. For example:
 *
 * export VITE_OWNTONE_URL=http://owntone.local:3689; npm run serve
 */
const target = process.env.VITE_OWNTONE_URL ?? 'http://localhost:3689'

export default defineConfig({
  build: {
    outDir: './htdocs',
    rollupOptions: {
      output: {
        assetFileNames: `assets/[name].[ext]`,
        chunkFileNames: `assets/[name].js`,
        entryFileNames: `assets/[name].js`
      }
    }
  },
  plugins: [
    vue(),
    i18n({
      include: path.resolve(__dirname, './src/i18n/**.json')
    })
  ],
  resolve: { alias: { '@': '/src' } },
  server: {
    proxy: {
      '/api': { target },
      '/ws': { target, ws: true },
      '/artwork': { target },
      '/stream.mp3': { target }
    }
  }
})
