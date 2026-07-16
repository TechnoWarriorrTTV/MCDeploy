import { query } from './db.mjs'
import { hashWithSecret } from './crypto.mjs'
import { HttpError } from './http.mjs'

export async function enforceRateLimit(kind, identifier, { limit = 5, windowSeconds = 600, blockSeconds = 600 } = {}) {
  const keyHash = await hashWithSecret(`${kind}:${String(identifier).toLowerCase()}`, 'RATE_LIMIT_PEPPER')
  const result = await query(`
    INSERT INTO request_rate_limits (key_hash, window_started_at, request_count, updated_at)
    VALUES ($1, now(), 1, now())
    ON CONFLICT (key_hash) DO UPDATE SET
      window_started_at = CASE WHEN request_rate_limits.window_started_at <= now() - ($2 * interval '1 second') THEN now() ELSE request_rate_limits.window_started_at END,
      request_count = CASE WHEN request_rate_limits.window_started_at <= now() - ($2 * interval '1 second') THEN 1 ELSE request_rate_limits.request_count + 1 END,
      blocked_until = CASE
        WHEN request_rate_limits.blocked_until > now() THEN request_rate_limits.blocked_until
        WHEN request_rate_limits.window_started_at <= now() - ($2 * interval '1 second') THEN NULL
        WHEN request_rate_limits.request_count + 1 > $3 THEN now() + ($4 * interval '1 second')
        ELSE NULL
      END,
      updated_at = now()
    RETURNING request_count, blocked_until
  `, [keyHash, windowSeconds, limit, blockSeconds])
  if (result.rows[0].blocked_until && new Date(result.rows[0].blocked_until) > new Date()) throw new HttpError(429, 'Too many requests. Please try again later.')
}
