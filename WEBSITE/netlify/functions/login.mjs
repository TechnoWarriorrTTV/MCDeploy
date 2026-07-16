import { query, transaction } from '../lib/db.mjs'
import { hashPassword, hashWithSecret, randomToken, verifyPassword } from '../lib/crypto.mjs'
import { HttpError, json, normalizeEmail, readJson, requestIp, requireMethod, route, validEmail } from '../lib/http.mjs'
import { enforceRateLimit } from '../lib/rate-limit.mjs'

const INVALID_CREDENTIALS = 'Invalid email or password.'

export default route(async request => {
  requireMethod(request, 'POST')
  const body = await readJson(request, { fields: ['email', 'password'] })
  const email = normalizeEmail(body.email)
  if (!validEmail(email) || typeof body.password !== 'string' || body.password.length < 1 || body.password.length > 128) throw new HttpError(400, INVALID_CREDENTIALS)
  await Promise.all([
    enforceRateLimit('login-ip', requestIp(request)),
    enforceRateLimit('login-email', email)
  ])
  const user = (await query('SELECT id, email, password_hash, status FROM users WHERE email = $1', [email])).rows[0]
  let passwordMatches = false
  if (user?.password_hash) passwordMatches = await verifyPassword(body.password, user.password_hash)
  else await hashPassword(body.password)
  if (!user || !passwordMatches || user.status !== 'active') throw new HttpError(401, INVALID_CREDENTIALS)
  const token = randomToken(32)
  const tokenHash = await hashWithSecret(token, 'SESSION_PEPPER')
  await transaction(async client => {
    const active = await client.query("UPDATE users SET last_seen_at = now() WHERE id = $1 AND status = 'active' RETURNING id", [user.id])
    if (!active.rows[0]) throw new HttpError(401, INVALID_CREDENTIALS)
    await client.query("INSERT INTO sessions (token_hash, user_id, expires_at) VALUES ($1, $2, now() + interval '24 hours')", [tokenHash, user.id])
  })
  return json({ status: 'success', token, email: user.email, expires_in: 86400 })
})

export const config = { path: '/api/webpanel/login' }
