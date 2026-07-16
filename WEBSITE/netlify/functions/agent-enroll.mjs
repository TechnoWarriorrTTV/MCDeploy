import { hashWithSecret, randomToken } from '../lib/crypto.mjs'
import { transaction } from '../lib/db.mjs'
import { HttpError, json, readJson, requestIp, requireMethod, route } from '../lib/http.mjs'
import { enforceRateLimit } from '../lib/rate-limit.mjs'

export const config = { path: '/api/agent/enroll' }

const invalidCode = () => new HttpError(400, 'Invalid or expired enrollment code.')

export default route(async request => {
  requireMethod(request, 'POST')
  const body = await readJson(request, {
    maxBytes: 8192,
    fields: ['code', 'installation_name', 'version']
  })
  const code = typeof body.code === 'string' ? body.code.trim() : ''
  const requestedName = typeof body.installation_name === 'string' ? body.installation_name.trim() : ''
  const version = typeof body.version === 'string' ? body.version.trim() : ''
  if (!/^\d{6}$/.test(code)) throw invalidCode()
  if (requestedName.length < 1 || requestedName.length > 100) throw new HttpError(400, 'A valid installation name is required.')
  if (version.length > 64) throw new HttpError(400, 'Version is too long.')

  await enforceRateLimit('agent-enroll', requestIp(request), {
    limit: 10,
    windowSeconds: 600,
    blockSeconds: 900
  })

  const codeHash = await hashWithSecret(code, 'AGENT_ENROLLMENT_SECRET')
  const token = randomToken(32)
  const credentialHash = await hashWithSecret(token, 'AGENT_ENROLLMENT_SECRET')

  const installation = await transaction(async client => {
    const selected = await client.query(`
      SELECT owner_user_id, installation_name, expires_at, consumed_at
      FROM enrollment_codes
      WHERE code_hash = $1
      FOR UPDATE
    `, [codeHash])
    const enrollment = selected.rows[0]
    if (!enrollment || enrollment.consumed_at || new Date(enrollment.expires_at) <= new Date()) throw invalidCode()

    const installationName = enrollment.installation_name || requestedName
    await client.query('UPDATE enrollment_codes SET consumed_at = now() WHERE code_hash = $1', [codeHash])
    const inserted = await client.query(`
      INSERT INTO installations
        (owner_user_id, name, credential_hash, status, version, last_seen_at)
      VALUES ($1, $2, $3, $4, $5, now())
      RETURNING id
    `, [enrollment.owner_user_id, installationName, credentialHash, 'online', version])
    const installationId = inserted.rows[0].id
    await client.query(`
      INSERT INTO audit_logs (user_id, installation_id, action, details)
      VALUES ($1, $2, $3, $4::jsonb)
    `, [
      enrollment.owner_user_id,
      installationId,
      'agent.installation.enrolled',
      JSON.stringify({ installation_name: installationName, version })
    ])
    return { id: installationId }
  })

  return json({
    status: 'ok',
    installation_id: installation.id,
    token
  }, 201)
})
