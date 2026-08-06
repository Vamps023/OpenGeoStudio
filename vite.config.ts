import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';
import pkg from './package.json';

export default defineConfig({
  plugins: [react()],
  define: {
    __APP_VERSION__: JSON.stringify(pkg.version),
  },
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './renderer'),
      '@components': path.resolve(__dirname, './renderer/components'),
      '@core': path.resolve(__dirname, './renderer/core'),
      '@types': path.resolve(__dirname, './shared/types'),
      '@shared': path.resolve(__dirname, './shared'),
      '@renderer': path.resolve(__dirname, './renderer'),
      '@modules': path.resolve(__dirname, './modules'),
      '@app': path.resolve(__dirname, './app'),
      '@ogscore': path.resolve(__dirname, './core'),
    },
  },
  root: '.',
  base: './',
  build: {
    outDir: 'dist',
    sourcemap: true,
    chunkSizeWarningLimit: 7000,
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes('@babylonjs')) return 'babylon';
          if (id.includes('maplibre-gl')) return 'maplibre';
          if (id.includes('node_modules/react') || id.includes('node_modules/react-dom') || id.includes('node_modules/zustand')) return 'react-vendor';
        },
      },
    },
  },
  optimizeDeps: {
    exclude: ['electron'],
  },
});
