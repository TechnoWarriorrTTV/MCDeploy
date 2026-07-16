import { requireAgent } from '../lib/auth.mjs'
import { transaction } from '../lib/db.mjs'
import { HttpError, json, readJson, requireMethod, route } from '../lib/http.mjs'

export const config = { path: '/api/agent/results' }

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i
const SENSITIVE_KEY = /(authorization|cookie|credential|password|secret|token|api[-_]?key|hash)/i
const LOCAL_PATH = /(?:[a-z]:[\\/]|\\\\[^\\/\s]+[\\/]|(?:^|[\s("'=])\/(?:home|users|root|tmp|var|etc|opt|srv|mnt|volumes)\/)/i
const MAX_RESULT_BYTES = 32768
const MAX_NODES = 2000

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
}

function sanitizeResult(value) {
  let nodes = 0
  const visit = (current, depth = 0) => {
    nodes += 1
    if (nodes > MAX_NODES) throw new HttpError(400, 'Result is too complex.')
    if (depth > 8) return '[truncated]'
    if (typeof current === 'string') {
      if (LOCAL_PATH.test(current)) return '[redacted]'
      return current.length > 8192 ? `${current.slice(0, 8192)}…` : current
    }
    if (current === null || typeof current === 'boolean' || typeof current === 'number') return current
    if (Array.isArray(current)) return current.slice(0, 200).map(item => visit(item, depth + 1))
    if (!isObject(current)) return null

    const output = Object.create(null)
    for (const [key, item] of Object.entries(current).slice(0, 200)) {
      if (key.length > 128) continue
      output[key] = SENSITIVE_KEY.test(key) ? '[redacted]' : visit(item, depth + 1)
    }
    return output
  }

  const sanitized = visit(value)
  const serialized = JSON.stringify(sanitized)
  if (Buffer.byteLength(serialized, 'utf8') > MAX_RESULT_BYTES) throw new HttpError(413, 'Result is too large.')
  return serialized
}

export default route(async request => {
  requireMethod(request, 'POST')
  const installation = await requireAgent(request)
  const body = await readJson(request, {
    maxBytes: 65536,
    fields: ['command_id', 'success', 'result']
  })
  if (typeof body.command_id !== 'string' || !UUID_PATTERN.test(body.command_id)) {
    throw new HttpError(400, 'A valid command ID is required.')
  }
  if (typeof body.success !== 'boolean' || !isObject(body.result)) {
    throw new HttpError(400, 'A success flag and result object are required.')
  }
  const sanitizedResult = sanitizeResult(body.result)

  const accepted = await transaction(async client => {
    const selected = await client.query(`
      SELECT command.status, command.expires_at <= now() AS expired,
             result.command_id AS completed_command_id
      FROM agent_commands AS command
      LEFT JOIN agent_results AS result ON result.command_id = command.id
      WHERE command.id = $1 AND command.installation_id = $2
      FOR UPDATE OF command
    `, [body.command_id, installation.id])
    const command = selected.rows[0]
    if (!command) throw new HttpError(404, 'Command not found.')
    if (command.completed_command_id) return { idempotent: true }
    if (command.expired) throw new HttpError(409, 'Command has expired.')
    if (command.status !== 'claimed') throw new HttpError(409, 'Command is not in a claimable result state.')

    await client.query(`
      INSERT INTO agent_results (command_id, success, result)
      VALUES ($1, $2, $3::jsonb)
    `, [body.command_id, body.success, sanitizedResult])
    await client.query(`
      UPDATE agent_commands
      SET status = $2
      WHERE id = $1
    `, [body.command_id, body.success ? 'succeeded' : 'failed'])
    return { idempotent: false }
  })

  return json({
    status: 'accepted',
    command_id: body.command_id,
    idempotent: accepted.idempotent
  })
})
