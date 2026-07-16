import { randomUUID } from 'node:crypto'
import { transaction } from './db.mjs'
import { HttpError } from './http.mjs'

export const COMMAND_TTL_SECONDS = 120

const IDEMPOTENCY_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._:-]{7,127}$/
const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i
const SENSITIVE_KEY = /(authorization|cookie|credential|password|secret|token|api[-_]?key|hash)/i
const LOCAL_PATH = /(?:^[a-z]:[\\/]|^\\\\|^\/(?:home|users|root|tmp|var|etc|opt|srv|mnt|volumes)(?:\/|$))/i

function idempotencyKey(request) {
  const supplied = request.headers.get('idempotency-key')
  if (supplied === null) return randomUUID()
  const key = supplied.trim()
  if (!IDEMPOTENCY_PATTERN.test(key)) throw new HttpError(400, 'Invalid Idempotency-Key header.')
  return key
}

function safeResult(value) {
  let nodes = 0
  const visit = (current, depth = 0) => {
    nodes += 1
    if (nodes > 2000 || depth > 8) return '[truncated]'
    if (typeof current === 'string') {
      if (LOCAL_PATH.test(current)) return '[redacted]'
      return current.length > 8192 ? `${current.slice(0, 8192)}…` : current
    }
    if (current === null || typeof current === 'boolean' || typeof current === 'number') return current
    if (Array.isArray(current)) return current.slice(0, 200).map(item => visit(item, depth + 1))
    if (!current || typeof current !== 'object') return null
    const output = Object.create(null)
    for (const [key, item] of Object.entries(current).slice(0, 200)) {
      if (key.length <= 128) output[key] = SENSITIVE_KEY.test(key) ? '[redacted]' : visit(item, depth + 1)
    }
    return output
  }
  return visit(value)
}
function assertLifecycle(action, status) {
  if (action === 'server.start' && ['Online', 'Starting'].includes(status)) {
    throw new HttpError(409, 'The server is already online or starting.')
  }
  if (action === 'server.stop' && !['Online', 'Starting'].includes(status)) {
    throw new HttpError(409, 'Only an online or starting server can be stopped.')
  }
  if (action === 'server.restart' && status !== 'Online') {
    throw new HttpError(409, 'Only an online server can be restarted.')
  }
}

export async function queueCommand({ request, userId, membership, action, payload, lifecycle = false }) {
  const key = idempotencyKey(request)
  const serializedPayload = JSON.stringify(payload)

  const command = await transaction(async client => {
    const prior = await client.query(`
      SELECT id, requested_by, server_uuid, action
      FROM agent_commands
      WHERE idempotency_key = $1
      FOR UPDATE
    `, [key])
    if (prior.rows[0]) {
      const row = prior.rows[0]
      if (row.requested_by !== userId || row.server_uuid !== membership.uuid || row.action !== action) {
        throw new HttpError(409, 'Idempotency-Key was already used for another operation.')
      }
      return row
    }

    const server = await client.query(`
      SELECT s.installation_id,
             CASE
               WHEN i.status <> $2
                AND i.last_seen_at IS NOT NULL
                AND i.last_seen_at > now() - make_interval(secs => $3)
               THEN s.status
               ELSE $4
             END AS effective_status
      FROM servers s
      JOIN installations i ON i.id = s.installation_id
      WHERE s.uuid = $1
      FOR UPDATE OF s
    `, [membership.uuid, 'suspended', 90, 'Offline'])
    if (!server.rows[0] || server.rows[0].installation_id !== membership.installation_id) {
      throw new HttpError(404, 'Server not found.')
    }
    if (lifecycle) assertLifecycle(action, server.rows[0].effective_status)

    const inserted = await client.query(`
      INSERT INTO agent_commands
        (installation_id, server_uuid, requested_by, action, payload,
         status, idempotency_key, expires_at)
      VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7,
              now() + make_interval(secs => $8))
      ON CONFLICT (idempotency_key) DO NOTHING
      RETURNING id, requested_by, server_uuid, action
    `, [
      membership.installation_id, membership.uuid, userId, action,
      serializedPayload, 'pending', key, COMMAND_TTL_SECONDS
    ])

    let row = inserted.rows[0]
    const created = Boolean(row)
    if (!row) {
      const existing = await client.query(`
        SELECT id, requested_by, server_uuid, action
        FROM agent_commands
        WHERE idempotency_key = $1
        FOR UPDATE
      `, [key])
      row = existing.rows[0]
      if (!row || row.requested_by !== userId || row.server_uuid !== membership.uuid || row.action !== action) {
        throw new HttpError(409, 'Idempotency-Key was already used for another operation.')
      }
    }

    if (created) {
      await client.query(`
        INSERT INTO audit_logs
          (user_id, installation_id, server_uuid, action, details)
        VALUES ($1, $2, $3, $4, $5::jsonb)
      `, [
        userId, membership.installation_id, membership.uuid, 'webpanel.command.queued',
        JSON.stringify({ command_id: row.id, action })
      ])
    }
    return row
  })

  return {
    command_id: command.id,
    status: 'pending',
    expires_in: COMMAND_TTL_SECONDS
  }
}
export async function commandStatus(userId, commandId) {
  if (!UUID_PATTERN.test(commandId)) throw new HttpError(400, 'Invalid command ID.')

  const command = await transaction(async client => {
    await client.query(`
      UPDATE agent_commands
      SET status = $3
      WHERE id = $1
        AND requested_by = $2
        AND status IN ($4, $5)
        AND expires_at <= now()
    `, [commandId, userId, 'expired', 'pending', 'claimed'])

    const selected = await client.query(`
      SELECT command.id, command.status,
             GREATEST($3, CEIL(EXTRACT(EPOCH FROM command.expires_at - now())))::integer AS expires_in,
             result.result
      FROM agent_commands AS command
      LEFT JOIN agent_results AS result ON result.command_id = command.id
      WHERE command.id = $1 AND command.requested_by = $2
    `, [commandId, userId, 0])
    return selected.rows[0]
  })

  if (!command) throw new HttpError(404, 'Command not found.')
  const response = {
    command_id: command.id,
    status: command.status,
    expires_in: Number(command.expires_in)
  }
  if (command.status === 'succeeded' && command.result && typeof command.result === 'object') {
    response.result = safeResult(command.result)
  }
  if (command.status === 'failed') {
    response.result = command.result && typeof command.result === 'object'
      ? safeResult(command.result)
      : { message: 'The MCDeploy agent could not complete the request.' }
  }
  return response
}
