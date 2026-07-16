import { cleanupExpired, transaction } from '../lib/db.mjs'
import { json, route } from '../lib/http.mjs'

export default route(async () => {
  await transaction(cleanupExpired)
  return json({ status: 'success' })
})

export const config = { schedule: '@hourly' }
