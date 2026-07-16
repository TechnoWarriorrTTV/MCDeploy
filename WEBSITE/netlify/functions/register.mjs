import { query } from '../lib/db.mjs'
import { hashPassword, hashWithSecret, randomSixDigitCode } from '../lib/crypto.mjs'
import { sendVerificationEmail } from '../lib/email.mjs'
import { HttpError, json, normalizeEmail, readJson, requestIp, requireMethod, route, validEmail } from '../lib/http.mjs'
import { enforceRateLimit } from '../lib/rate-limit.mjs'

export default route(async request => {
  requireMethod(request, 'POST')
  const body = await readJson(request, { fields: ['email', 'password', 'display_name'] })
  const email = normalizeEmail(body.email)
  const password = body.password
  const displayName = body.display_name === undefined ? '' : body.display_name
  if (!validEmail(email) || typeof password !== 'string' || password.length < 10 || password.length > 128 || typeof displayName !== 'string' || [...displayName].length > 80) {
    throw new HttpError(400, 'Invalid registration details.')
  }
  if ((await query('SELECT 1 FROM users WHERE email = $1', [email])).rows[0]) throw new HttpError(409, 'An account with this email already exists.')
  await Promise.all([
    enforceRateLimit('register-ip', requestIp(request)),
    enforceRateLimit('register-email', email)
  ])
  const code = randomSixDigitCode()
  const [passwordHash, verificationHash] = await Promise.all([
    hashPassword(password),
    hashWithSecret(`${email}:${code}`, 'VERIFICATION_PEPPER')
  ])
  const pending = await query(`
    INSERT INTO pending_registrations (email, display_name, password_hash, verification_hash, expires_at, resend_after, attempts, created_at)
    SELECT $1, $2, $3, $4, now() + interval '10 minutes', now() + interval '60 seconds', 0, now()
    WHERE NOT EXISTS (SELECT 1 FROM users WHERE email = $1)
    ON CONFLICT (email) DO UPDATE SET display_name = EXCLUDED.display_name, password_hash = EXCLUDED.password_hash,
      verification_hash = EXCLUDED.verification_hash, expires_at = EXCLUDED.expires_at,
      resend_after = EXCLUDED.resend_after, attempts = 0, created_at = now()
    WHERE pending_registrations.resend_after <= now()
    RETURNING email
  `, [email, displayName, passwordHash, verificationHash])
  if (!pending.rows[0]) throw new HttpError(429, 'Please wait before requesting another verification email.')
  try {
    await sendVerificationEmail(email, code)
  } catch {
    await query('DELETE FROM pending_registrations WHERE email = $1 AND verification_hash = $2', [email, verificationHash])
    throw new HttpError(502, 'Verification email could not be delivered.')
  }
  return json({ status: 'pending_verification', email, expires_in: 600 }, 202)
})

export const config = { path: '/api/webpanel/register' }
