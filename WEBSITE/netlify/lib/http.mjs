export class HttpError extends Error {
  constructor(status, message) {
    super(message)
    this.status = status
  }
}

export const json = (body, status = 200, headers = {}) => Response.json(body, {
  status,
  headers: { 'Cache-Control': 'no-store', ...headers }
})

export const error = (message, status = 400) => json({ status: 'error', message }, status)

export function requireMethod(request, allowed) {
  const methods = Array.isArray(allowed) ? allowed : [allowed]
  if (!methods.includes(request.method)) throw new HttpError(405, 'Method not allowed.')
}

export async function readJson(request, { maxBytes = 65536, fields } = {}) {
  const contentType = request.headers.get('content-type') || ''
  if (!contentType.toLowerCase().startsWith('application/json')) throw new HttpError(400, 'A JSON request body is required.')
  const declared = Number(request.headers.get('content-length') || 0)
  if (declared > maxBytes) throw new HttpError(413, 'Request body is too large.')
  const text = await request.text()
  if (!text || new TextEncoder().encode(text).length > maxBytes) throw new HttpError(400, 'Invalid request body.')
  let value
  try { value = JSON.parse(text) } catch { throw new HttpError(400, 'Invalid JSON.') }
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new HttpError(400, 'Invalid request body.')
  if (fields) {
    const unknown = Object.keys(value).find(key => !fields.includes(key))
    if (unknown) throw new HttpError(400, 'Request contains unsupported fields.')
  }
  return value
}

export function requestIp(request) {
  return (request.headers.get('x-nf-client-connection-ip') || request.headers.get('x-forwarded-for')?.split(',')[0] || 'unknown').trim()
}

export function route(handler) {
  return async request => {
    try {
      return await handler(request)
    } catch (caught) {
      if (caught instanceof HttpError) return error(caught.message, caught.status)
      return error('The service is temporarily unavailable.', 500)
    }
  }
}

export const normalizeEmail = value => typeof value === 'string' ? value.trim().toLowerCase() : ''
export const validEmail = value => /^(?=.{3,254}$)[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
