import { query, transaction } from '../lib/db.mjs'
import { hashWithSecret, randomSixDigitCode } from '../lib/crypto.mjs'
import { sendVerificationEmail } from '../lib/email.mjs'
import { HttpError, json, normalizeEmail, readJson, requestIp, requireMethod, route, validEmail } from '../lib/http.mjs'
import { enforceRateLimit } from '../lib/rate-limit.mjs'

export default route(async request => {
  requireMethod(request, 'POST')
  const body = await readJson(request, { fields: ['email'] })
  const email = normalizeEmail(body.email)
  if (!validEmail(email)) throw new HttpError(400, 'A valid email address is required.')
  await Promise.all([
    enforceRateLimit('register-resend-ip', requestIp(request)),
    enforceRateLimit('register-resend-email', email)
  ])
  const outcome = await transaction(async client => {
    const result = await client.query(`
      SELECT attempts, expires_at <= now() AS expired, resend_after > now() AS cooling_down
      FROM pending_registrations WHERE email = $1 FOR UPDATE
    `, [email])
    const pending = result.rows[0]
    if (!pending) return { error: 400, message: 'No pending registration was found.' }
    if (pending.expired) {
      await client.query('DELETE FROM pending_registrations WHERE email = $1', [email])
      return { error: 400, message: 'No pending registration was found.' }
    }
    if (pending.attempts >= 5) return { error: 429, message: 'Too many verification attempts.' }
    if (pending.cooling_down) return { error: 429, message: 'Please wait before requesting another code.' }
    const code = randomSixDigitCode()
    const verificationHash = await hashWithSecret(`${email}:${code}`, 'VERIFICATION_PEPPER')
    await client.query(`
      UPDATE pending_registrations SET verification_hash = $2, expires_at = now() + interval '10 minutes',
        resend_after = now() + interval '60 seconds' WHERE email = $1
    `, [email, verificationHash])
    return { code, verificationHash }
  })
  if (outcome.error) throw new HttpError(outcome.error, outcome.message)
  try {
    await sendVerificationEmail(email, outcome.code)
  } catch {
    await query('DELETE FROM pending_registrations WHERE email = $1 AND verification_hash = $2', [email, outcome.verificationHash])
    throw new HttpError(502, 'Verification email could not be delivered.')
  }
  return json({ status: 'success', expires_in: 600 })
})

export const config = { path: '/api/webpanel/register/resend' }
