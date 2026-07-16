import { json, requireMethod, route } from '../lib/http.mjs'

export default route(async request => {
  requireMethod(request, 'GET')
  return json({
    status: 'success',
    api_version: 4,
    email_verification: Boolean(process.env.RESEND_API_KEY && process.env.RESEND_FROM),
    oauth_providers: []
  })
})

export const config = { path: '/api/webpanel/status' }
