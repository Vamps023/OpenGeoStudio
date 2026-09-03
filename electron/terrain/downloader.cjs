/**
 * HTTP download utilities with retry and redirect handling.
 */

const https = require('node:https')

function downloadBuffer(url, maxRedirects = 5, timeoutMs = 30000) {
  return new Promise((resolve, reject) => {
    if (maxRedirects <= 0) {
      reject(new Error(`Too many redirects for ${url}`))
      return
    }
    const req = https.get(
      url,
      {
        headers: {
          'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
          Accept: '*/*',
        },
      },
      (res) => {
        if (res.statusCode === 301 || res.statusCode === 302 || res.statusCode === 307 || res.statusCode === 308) {
          const location = res.headers.location
          if (location) {
            res.resume()
            downloadBuffer(location, maxRedirects - 1, timeoutMs).then(resolve).catch(reject)
            return
          }
        }
        if (res.statusCode !== 200) {
          res.resume()
          reject(new Error(`HTTP ${res.statusCode} for ${url}`))
          return
        }
        const chunks = []
        res.on('data', (chunk) => chunks.push(chunk))
        res.on('end', () => resolve(Buffer.concat(chunks)))
        res.on('error', reject)
      },
    )
    req.on('error', reject)
    const deadline = setTimeout(() => req.destroy(new Error(`Timeout after ${timeoutMs}ms`)), timeoutMs)
    req.on('close', () => clearTimeout(deadline))
  })
}

async function downloadWithRetry(url, retries = 3, timeoutMs = 30000) {
  for (let i = 0; i < retries; i++) {
    try {
      return await downloadBuffer(url, 5, timeoutMs)
    } catch (err) {
      if (i === retries - 1) throw err
      const isRateLimit = err.message.includes('429')
      const delay = isRateLimit ? 1000 * Math.pow(2, i + 2) : 500 * Math.pow(2, i)
      await new Promise((r) => setTimeout(r, delay))
    }
  }
  throw new Error('Unreachable')
}

module.exports = { downloadBuffer, downloadWithRetry }
