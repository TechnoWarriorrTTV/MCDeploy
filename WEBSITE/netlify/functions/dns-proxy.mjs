import { json, HttpError, route } from '../lib/http.mjs'

export default route(async request => {
  // 1. Verify Secret if DNS_PROXY_SECRET is configured
  const serverSecret = process.env.DNS_PROXY_SECRET
  if (serverSecret && serverSecret.length > 0) {
    const clientSecret = request.headers.get('x-mcdeploy-dns-secret') || ''
    if (clientSecret !== serverSecret) {
      throw new HttpError(401, 'Unauthorized proxy request.')
    }
  }

  // 2. Extract Cloudflare target endpoint from header
  let endpoint = request.headers.get('x-mcdeploy-dns-endpoint') || ''
  if (!endpoint) {
    throw new HttpError(400, 'Missing x-mcdeploy-dns-endpoint header.')
  }

  // 3. Inject secure Zone ID from Netlify environment variables
  const zoneId = process.env.CLOUDFLARE_ZONE_ID
  if (!zoneId) {
    throw new HttpError(500, 'Cloudflare Zone ID is not configured on the server.')
  }

  // Rewrite `/zones/default/` to `/zones/REAL_ZONE_ID/`
  if (endpoint.startsWith('/zones/default/')) {
    endpoint = endpoint.replace('/zones/default/', `/zones/${zoneId}/`)
  } else {
    // Validation: only allow accessing the configured zone
    const match = endpoint.match(/^\/zones\/([^/]+)/)
    if (match && match[1] !== 'default' && match[1] !== zoneId) {
      throw new HttpError(403, 'Forbidden: Access to this Cloudflare zone is not allowed.')
    }
  }

  // 4. Forward the request to Cloudflare API
  const cloudflareApiToken = process.env.CLOUDFLARE_API_TOKEN
  if (!cloudflareApiToken) {
    throw new HttpError(500, 'Cloudflare API Token is not configured on the server.')
  }

  const url = `https://api.cloudflare.com/client/v4${endpoint}`
  const method = request.method
  const headers = {
    'Authorization': `Bearer ${cloudflareApiToken}`,
    'Content-Type': 'application/json',
    'Accept': 'application/json'
  }

  // Parse body if it has one
  let body = null
  if (method === 'POST' || method === 'PUT') {
    body = await request.text()
  }

  try {
    const response = await fetch(url, {
      method,
      headers,
      body: body || undefined
    })

    const responseText = await response.text()
    
    // Return the response back to C++ client with the same status code
    let parsedJson
    try {
      parsedJson = JSON.parse(responseText)
    } catch {
      // If it's not JSON, return text
      return new Response(responseText, {
        status: response.status,
        headers: { 'Content-Type': 'text/plain' }
      })
    }

    return json(parsedJson, response.status)

  } catch (err) {
    console.error('[DNS Proxy] Cloudflare fetch error:', err)
    throw new HttpError(502, 'Bad Gateway: Failed to forward request to Cloudflare.')
  }
})

export const config = { path: '/api/dns/proxy' }
