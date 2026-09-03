import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const cspPlugin = {
  name: 'ogs-csp',
  transformIndexHtml(html, ctx) {
    const dev = Boolean(ctx.server)
    // Allow map tile servers (Esri, AWS Terrarium, Mapbox, Copernicus S3),
    // DEM APIs (OpenTopography, GPXZ), and OpenFreeMap vector tiles.
    const tileSources =
      'https://server.arcgisonline.com ' +
      'https://s3.amazonaws.com ' +
      'https://api.mapbox.com ' +
      'https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com ' +
      'https://portal.opentopography.org ' +
      'https://api.gpxz.io ' +
      'https://tiles.openfreemap.org ' +
      'https://mt1.google.com ' +
      'https://mt2.google.com ' +
      'https://mt3.google.com ' +
      'https://api.maptiler.com'
    const csp = dev
      ? `default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline' https://api.mapbox.com; img-src 'self' data: blob: ${tileSources}; connect-src 'self' ws: ${tileSources}; worker-src 'self' blob:`
      : `default-src 'self'; script-src 'self'; style-src 'self' https://api.mapbox.com; img-src 'self' data: blob: ${tileSources}; connect-src 'self' ${tileSources}; worker-src 'self' blob:`
    return html.replace('</head>', `    <meta http-equiv="Content-Security-Policy" content="${csp}" />\n  </head>`)
  },
}

export default defineConfig({
  base: './',
  plugins: [react(), cspPlugin],
})
