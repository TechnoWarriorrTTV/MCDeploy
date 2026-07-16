import { requireSession } from '../lib/auth.mjs'
import { json, requireMethod, route } from '../lib/http.mjs'

export default route(async request => {
  requireMethod(request, 'GET')
  const session = await requireSession(request)
  return json({ status: 'success', email: session.email })
})

export const config = { path: '/api/webpanel/session' }
