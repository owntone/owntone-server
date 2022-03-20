import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// Support for setting the OwnTone server URL as env var VITE_OWNTONE_URL
// in development mode.
// E. g. start the DEV server with
//
//     VITE_OWNTONE_URL=https://owntone.local:3689 npm run serve
//
// will connect the web interface with a remote OwnTone server.
const owntoneUrl = process.env.VITE_OWNTONE_URL ?? 'http://localhost:3689'

// https://vitejs.dev/config/
export default defineConfig({
  resolve: { alias: { '@': '/src' } },
  plugins: [vue()],
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
