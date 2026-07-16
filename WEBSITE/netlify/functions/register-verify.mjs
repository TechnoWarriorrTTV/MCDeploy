import { transaction } from '../lib/db.mjs'
import { constantTimeEqual, hashWithSecret, randomToken } from '../lib/crypto.mjs'
import { HttpError, json, normalizeEmail, readJson, requestIp, requireMethod, route, validEmail } from '../lib/http.mjs'
import { enforceRateLimit } from '../lib/rate-limit.mjs'

export default route(async request => {
  requireMethod(request, 'POST')
  const body = await readJson(request, { fields: ['email', 'code'] })
  const email = normalizeEmail(body.email)
  if (!validEmail(email) || typeof body.code !== 'string' || !/^\d{6}$/.test(body.code)) throw new HttpError(400, 'Invalid verification request.')
  await Promise.all([
    enforceRateLimit('register-verify-ip', requestIp(request), { limit: 20 }),
    enforceRateLimit('register-verify-email', email, { limit: 5 })
  ])
  const outcome = await transaction(async client => {
    const result = await client.query(`
      SELECT display_name, password_hash, verification_hash, attempts, expires_at <= now() AS expired
      FROM pending_registrations WHERE email = $1 FOR UPDATE
    `, [email])
    const pending = result.rows[0]
    if (!pending) return { error: 400, message: 'Invalid or expired verification code.' }
    if (pending.expired) {
      await client.query('DELETE FROM pending_registrations WHERE email = $1', [email])
      return { error: 400, message: 'Invalid or expired verification code.' }
    }
    if (pending.attempts >= 5) return { error: 429, message: 'Too many verification attempts.' }
    const candidate = await hashWithSecret(`${email}:${body.code}`, 'VERIFICATION_PEPPER')
    if (!constantTimeEqual(candidate, pending.verification_hash)) {
      await client.query('UPDATE pending_registrations SET attempts = LEAST(attempts + 1, 5) WHERE email = $1 AND attempts < 5', [email])
      return { error: 400, message: 'Invalid or expired verification code.' }
    }
    const user = await client.query(`
      INSERT INTO users (email, display_name, password_hash) VALUES ($1, $2, $3)
      ON CONFLICT (email) DO NOTHING RETURNING id, email
    `, [email, pending.display_name, pending.password_hash])
    await client.query('DELETE FROM pending_registrations WHERE email = $1', [email])
    if (!user.rows[0]) return { error: 409, message: 'An account with this email already exists.' }
    const token = randomToken(32)
    const tokenHash = await hashWithSecret(token, 'SESSION_PEPPER')
    await client.query("INSERT INTO sessions (token_hash, user_id, expires_at) VALUES ($1, $2, now() + interval '24 hours')", [tokenHash, user.rows[0].id])
    return { token, email: user.rows[0].email }
  })
  if (outcome.error) throw new HttpError(outcome.error, outcome.message)
  return json({ status: 'success', token: outcome.token, email: outcome.email, expires_in: 86400 }, 201)
})

export const config = { path: '/api/webpanel/register/verify' }
