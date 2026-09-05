/**
 * Overpass API fetcher (main process).
 *
 * The renderer builds the Overpass QL query string (see
 * src/engine/osmBuildings.ts — overpassQuery / overpassQueryPolygon) and
 * passes it here. Performing the request from the main process avoids the
 * browser CORS restriction that blocks `app://localhost` from calling
 * overpass-api.de directly (see issue #46).
 *
 * Building data (c) OpenStreetMap contributors (ODbL 1.0).
 */

const https = require('node:https')
const { URL } = require('node:url')

const OVERPASS_ENDPOINT = 'https://overpass-api.de/api/interpreter'
const TIMEOUT_MS = 60000

/** POST an Overpass QL `query` and return the parsed JSON response. */
function fetchOverpass(query) {
  return new Promise((resolve, reject) => {
    const endpoint = new URL(OVERPASS_ENDPOINT)
    const body = 'data=' + encodeURIComponent(query)
    const options = {
      method: 'POST',
      hostname: endpoint.hostname,
      path: endpoint.pathname + endpoint.search,
      port: endpoint.port || 443,
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': Buffer.byteLength(body),
        'User-Agent': 'OpenGeoStudio/1.0 (desktop app; OSM building importer)',
        Accept: 'application/json',
      },
    }
    const req = https.request(options, (res) => {
      if (res.statusCode === 301 || res.statusCode === 302 || res.statusCode === 307 || res.statusCode === 308) {
        const location = res.headers.location
        res.resume()
        if (location) {
          fetchOverpassRedirect(location, body).then(resolve).catch(reject)
        } else {
          reject(new Error(`Overpass redirect with no Location header (${res.statusCode})`))
        }
        return
      }
      if (res.statusCode !== 200) {
        const chunks = []
        res.on('data', (c) => chunks.push(c))
        res.on('end', () => {
          const detail = Buffer.concat(chunks).toString('utf-8').slice(0, 200)
          reject(new Error(`Overpass API returned ${res.statusCode}: ${detail}`))
        })
        return
      }
      const chunks = []
      res.on('data', (chunk) => chunks.push(chunk))
      res.on('end', () => {
        try {
          resolve(JSON.parse(Buffer.concat(chunks).toString('utf-8')))
        } catch (err) {
          reject(new Error('Overpass returned non-JSON response: ' + err.message))
        }
      })
      res.on('error', reject)
    })
    req.on('error', reject)
    const deadline = setTimeout(() => req.destroy(new Error(`Overpass request timed out after ${TIMEOUT_MS}ms`)), TIMEOUT_MS)
    req.on('close', () => clearTimeout(deadline))
    req.write(body)
    req.end()
  })
}

/** Follow a redirect to an absolute `location` URL, replaying the same body. */
function fetchOverpassRedirect(location, body) {
  return new Promise((resolve, reject) => {
    const endpoint = new URL(location)
    const options = {
      method: 'POST',
      hostname: endpoint.hostname,
      path: endpoint.pathname + endpoint.search,
      port: endpoint.port || (endpoint.protocol === 'https:' ? 443 : 80),
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': Buffer.byteLength(body),
        'User-Agent': 'OpenGeoStudio/1.0 (desktop app; OSM building importer)',
        Accept: 'application/json',
      },
    }
    const lib = endpoint.protocol === 'https:' ? https : require('node:http')
    const req = lib.request(options, (res) => {
      if (res.statusCode !== 200) {
        res.resume()
        reject(new Error(`Overpass redirect returned ${res.statusCode}`))
        return
      }
      const chunks = []
      res.on('data', (chunk) => chunks.push(chunk))
      res.on('end', () => {
        try {
          resolve(JSON.parse(Buffer.concat(chunks).toString('utf-8')))
        } catch (err) {
          reject(new Error('Overpass returned non-JSON response: ' + err.message))
        }
      })
      res.on('error', reject)
    })
    req.on('error', reject)
    const deadline = setTimeout(() => req.destroy(new Error(`Overpass redirect timed out after ${TIMEOUT_MS}ms`)), TIMEOUT_MS)
    req.on('close', () => clearTimeout(deadline))
    req.write(body)
    req.end()
  })
}

module.exports = { fetchOverpass, OVERPASS_ENDPOINT }
