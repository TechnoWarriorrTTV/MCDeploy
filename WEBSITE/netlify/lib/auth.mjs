import { query } from './db.mjs'
import { constantTimeEqual, hashWithSecret } from './crypto.mjs'
import { HttpError } from './http.mjs'

function authorization(request, scheme) {
  const header = request.headers.get('authorization') || ''
  const prefix = `${scheme} `
  if (!header.startsWith(prefix)) throw new HttpError(401, 'Authentication required.')
  const value = header.slice(prefix.length).trim()
  if (!value || value.length > 512) throw new HttpError(401, 'Authentication required.')
  return value
}

export async function requireSession(request) {
  const token = authorization(request, 'Bearer')
  const tokenHash = await hashWithSecret(token, 'SESSION_PEPPER')
  const result = await query(`
    SELECT u.id, u.email, u.display_name
    FROM sessions s JOIN users u ON u.id = s.user_id
    WHERE s.token_hash = $1 AND s.expires_at > now() AND u.status = 'active'
  `, [tokenHash])
  if (!result.rows[0]) throw new HttpError(401, 'Invalid or expired session.')
  return result.rows[0]
}

export async function requireAgent(request) {
  const credential = authorization(request, 'Agent')
  const separator = credential.indexOf('.')
  if (separator < 1) throw new HttpError(401, 'Invalid agent credentials.')
  const installationId = credential.slice(0, separator)
  const token = credential.slice(separator + 1)
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(installationId) || token.length < 32) throw new HttpError(401, 'Invalid agent credentials.')
  const secret = process.env.AGENT_ENROLLMENT_SECRET
  if (!secret || secret.length < 32) throw new Error('Agent authentication is unavailable.')
  const result = await query('SELECT id, owner_user_id, credential_hash, status FROM installations WHERE id = $1', [installationId])
  const installation = result.rows[0]
  const candidate = await hashWithSecret(token, 'AGENT_ENROLLMENT_SECRET')
  if (!installation || installation.status === 'suspended' || !constantTimeEqual(candidate, installation.credential_hash)) {
    throw new HttpError(401, 'Invalid agent credentials.')
  }
  const version = (request.headers.get('x-mcdeploy-version') || '').trim().slice(0, 64)
  await query("UPDATE installations SET last_seen_at = now(), status = 'online', version = CASE WHEN $2 = '' THEN version ELSE $2 END WHERE id = $1", [installation.id, version])
  return installation
}
