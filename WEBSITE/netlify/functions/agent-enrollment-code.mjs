import { requireSession } from '../lib/auth.mjs'
import { hashWithSecret, randomSixDigitCode } from '../lib/crypto.mjs'
import { transaction } from '../lib/db.mjs'
import { HttpError, json, readJson, requireMethod, route } from '../lib/http.mjs'

const CODE_TTL_SECONDS = 600
const MAX_INSERT_ATTEMPTS = 5

export const config = { path: '/api/agent/enrollment-code' }

export default route(async request => {
  requireMethod(request, 'POST')
  const owner = await requireSession(request)
  const body = await readJson(request, { maxBytes: 4096, fields: ['installation_name'] })
  const installationName = typeof body.installation_name === 'string' ? body.installation_name.trim() : ''
  if (installationName.length > 100) throw new HttpError(400, 'Installation name is too long.')

  for (let attempt = 0; attempt < MAX_INSERT_ATTEMPTS; attempt += 1) {
    const code = randomSixDigitCode()
    const codeHash = await hashWithSecret(code, 'AGENT_ENROLLMENT_SECRET')
    try {
      const created = await transaction(async client => {
        const result = await client.query(`
          INSERT INTO enrollment_codes (code_hash, owner_user_id, installation_name, expires_at)
          VALUES ($1, $2, $3, now() + ($4 * interval '1 second'))
          RETURNING expires_at
        `, [codeHash, owner.id, installationName, CODE_TTL_SECONDS])
        await client.query(`
          INSERT INTO audit_logs (user_id, action, details)
          VALUES ($1, $2, $3::jsonb)
        `, [owner.id, 'agent.enrollment_code.created', JSON.stringify({
          installation_name: installationName,
          expires_in_seconds: CODE_TTL_SECONDS
        })])
        return result.rows[0]
      })
      return json({ status: 'ok', code, expires_at: created.expires_at }, 201)
    } catch (caught) {
      if (caught?.code !== '23505') throw caught
    }
  }

  throw new Error('Unable to allocate enrollment code.')
})
