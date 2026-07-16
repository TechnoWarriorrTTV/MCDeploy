import { requireAgent } from '../lib/auth.mjs'
import { transaction } from '../lib/db.mjs'
import { HttpError, json, normalizeEmail, readJson, requireMethod, route, validEmail } from '../lib/http.mjs'

export const config = { path: '/api/agent/servers/sync' }

const MAX_SERVERS = 100
const MAX_MEMBERS_PER_SERVER = 200
const MAX_TOTAL_MEMBERS = 2000
const SERVER_UUID = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/
const SUBDOMAIN = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/
const SERVER_STATUSES = new Set(['Online', 'Offline', 'Starting', 'Stopping', 'Error'])
const MEMBER_STATUSES = new Set(['active', 'suspended'])
const SENSITIVE_KEY = /(authorization|cookie|credential|password|secret|token|api[-_]?key|hash)/i
const LOCAL_PATH = /(?:[a-z]:[\\/]|\\\\[^\\/\s]+[\\/]|(?:^|[\s("'=])\/(?:home|users|root|tmp|var|etc|opt|srv|mnt|volumes)\/)/i

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
}

function assertFields(value, allowed) {
  if (Object.keys(value).some(key => !allowed.includes(key))) throw new HttpError(400, 'Payload contains unsupported fields.')
}

function stringValue(value, { min = 0, max, fallback = '' } = {}) {
  const normalized = value === undefined ? fallback : value
  if (typeof normalized !== 'string') throw new HttpError(400, 'Payload contains an invalid string.')
  const result = normalized.trim()
  if (result.length < min || result.length > max) throw new HttpError(400, 'Payload contains an invalid string.')
  return result
}

function integerValue(value, { min, max }) {
  if (!Number.isInteger(value) || value < min || value > max) throw new HttpError(400, 'Payload contains an invalid number.')
  return value
}

function sanitizeObject(value, maxBytes) {
  if (!isObject(value)) throw new HttpError(400, 'Metadata and permissions must be JSON objects.')
  let nodes = 0
  const visit = (current, depth = 0) => {
    nodes += 1
    if (nodes > 2000) throw new HttpError(400, 'Payload is too complex.')
    if (depth > 8) return '[truncated]'
    if (typeof current === 'string') {
      if (LOCAL_PATH.test(current)) return '[redacted]'
      return current.length > 4096 ? `${current.slice(0, 4096)}…` : current
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
  const serialized = JSON.stringify(visit(value))
  if (Buffer.byteLength(serialized, 'utf8') > maxBytes) throw new HttpError(413, 'Metadata or permissions are too large.')
  return serialized
}

function validateMember(raw) {
  if (!isObject(raw)) throw new HttpError(400, 'Each member must be an object.')
  assertFields(raw, ['email', 'role', 'permissions', 'status'])
  const email = normalizeEmail(raw.email)
  if (!validEmail(email)) throw new HttpError(400, 'A member email is invalid.')
  const role = stringValue(raw.role, { min: 1, max: 64 })
  const status = raw.status === undefined ? 'active' : raw.status
  if (!MEMBER_STATUSES.has(status)) throw new HttpError(400, 'A member status is invalid.')
  const permissions = sanitizeObject(raw.permissions ?? {}, 8192)
  return { email, role, status, permissions }
}

function validateServer(raw) {
  if (!isObject(raw)) throw new HttpError(400, 'Each server must be an object.')
  assertFields(raw, [
    'uuid', 'name', 'software_type', 'version', 'status', 'port',
    'ram_min', 'ram_max', 'subdomain', 'metadata', 'members'
  ])
  const uuid = stringValue(raw.uuid, { min: 1, max: 128 })
  if (!SERVER_UUID.test(uuid)) throw new HttpError(400, 'A server UUID is invalid.')
  const name = stringValue(raw.name, { min: 1, max: 100 })
  const softwareType = stringValue(raw.software_type, { max: 64 })
  const version = stringValue(raw.version, { max: 64 })
  const status = raw.status === undefined ? 'Offline' : raw.status
  if (!SERVER_STATUSES.has(status)) throw new HttpError(400, 'A server status is invalid.')
  const port = integerValue(raw.port ?? 25565, { min: 1, max: 65535 })
  const ramMin = integerValue(raw.ram_min ?? 0, { min: 0, max: 2147483647 })
  const ramMax = integerValue(raw.ram_max ?? 0, { min: ramMin, max: 2147483647 })
  const metadata = sanitizeObject(raw.metadata ?? {}, 16384)
  if (!Array.isArray(raw.members) || raw.members.length > MAX_MEMBERS_PER_SERVER) {
    throw new HttpError(400, 'Server members must be a bounded array.')
  }

  let subdomain = null
  if (raw.subdomain !== undefined && raw.subdomain !== null && raw.subdomain !== '') {
    subdomain = stringValue(raw.subdomain, { min: 1, max: 63 }).toLowerCase()
    if (!SUBDOMAIN.test(subdomain)) throw new HttpError(400, 'A subdomain is invalid.')
  }

  const members = raw.members.map(validateMember)
  if (new Set(members.map(member => member.email)).size !== members.length) {
    throw new HttpError(400, 'Member emails must be unique per server.')
  }
  return { uuid, name, softwareType, version, status, port, ramMin, ramMax, subdomain, metadata, members }
}

export default route(async request => {
  requireMethod(request, 'POST')
  const installation = await requireAgent(request)
  const body = await readJson(request, { maxBytes: 524288, fields: ['servers'] })
  if (!Array.isArray(body.servers) || body.servers.length > MAX_SERVERS) {
    throw new HttpError(400, 'Servers must be a bounded array.')
  }
  const servers = body.servers.map(validateServer)
  if (new Set(servers.map(server => server.uuid)).size !== servers.length) {
    throw new HttpError(400, 'Server UUIDs must be unique.')
  }
  const totalMembers = servers.reduce((total, server) => total + server.members.length, 0)
  if (totalMembers > MAX_TOTAL_MEMBERS) throw new HttpError(400, 'Too many server members.')

  try {
    const summary = await transaction(async client => {
      await client.query('SELECT id FROM installations WHERE id = $1 FOR UPDATE', [installation.id])
      const sortedUuids = servers.map(server => server.uuid).sort()
      for (const uuid of sortedUuids) {
        await client.query('SELECT pg_advisory_xact_lock(hashtextextended($1, 0))', [uuid])
        const existing = await client.query('SELECT installation_id FROM servers WHERE uuid = $1 FOR UPDATE', [uuid])
        if (existing.rows[0] && existing.rows[0].installation_id !== installation.id) {
          throw new HttpError(409, 'A server UUID is already assigned to another installation.')
        }
      }

      const emails = [...new Set(servers.flatMap(server => server.members.map(member => member.email)))]
      const users = await client.query('SELECT id, email FROM users WHERE email = ANY($1::text[])', [emails])
      const usersByEmail = new Map(users.rows.map(user => [user.email, user.id]))
      let synchronizedMembers = 0

      for (const server of servers) {
        const upserted = await client.query(`
          INSERT INTO servers
            (uuid, installation_id, name, software_type, version, status, port,
             ram_min, ram_max, metadata, last_synced_at)
          VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb, now())
          ON CONFLICT (uuid) DO UPDATE SET
            name = EXCLUDED.name,
            software_type = EXCLUDED.software_type,
            version = EXCLUDED.version,
            status = EXCLUDED.status,
            port = EXCLUDED.port,
            ram_min = EXCLUDED.ram_min,
            ram_max = EXCLUDED.ram_max,
            metadata = EXCLUDED.metadata,
            last_synced_at = now()
          WHERE servers.installation_id = EXCLUDED.installation_id
          RETURNING uuid
        `, [
          server.uuid, installation.id, server.name, server.softwareType, server.version,
          server.status, server.port, server.ramMin, server.ramMax, server.metadata
        ])
        if (!upserted.rows[0]) throw new HttpError(409, 'A server UUID is already assigned to another installation.')

        if (server.subdomain) {
          const reserved = await client.query(`
            INSERT INTO subdomain_reservations
              (subdomain, server_uuid, installation_id, state, updated_at)
            VALUES ($1, $2, $3, $4, now())
            ON CONFLICT (server_uuid) DO UPDATE SET
              subdomain = EXCLUDED.subdomain,
              state = $4,
              updated_at = now()
            WHERE subdomain_reservations.installation_id = EXCLUDED.installation_id
            RETURNING subdomain
          `, [server.subdomain, server.uuid, installation.id, 'reserved'])
          if (!reserved.rows[0]) throw new HttpError(409, 'Subdomain is already reserved.')
          await client.query(`
            UPDATE servers
            SET subdomain = $2
            WHERE uuid = $1 AND installation_id = $3
          `, [server.uuid, server.subdomain, installation.id])
        }

        const desiredMembers = server.members
          .map(member => ({ ...member, userId: usersByEmail.get(member.email) }))
          .filter(member => member.userId)
        const desiredUserIds = desiredMembers.map(member => member.userId)
        await client.query(`
          DELETE FROM server_members
          WHERE server_uuid = $1
            AND NOT (user_id = ANY($2::uuid[]))
        `, [server.uuid, desiredUserIds])
        for (const member of desiredMembers) {
          await client.query(`
            INSERT INTO server_members
              (server_uuid, user_id, role, permissions, status)
            VALUES ($1, $2, $3, $4::jsonb, $5)
            ON CONFLICT (server_uuid, user_id) DO UPDATE SET
              role = EXCLUDED.role,
              permissions = EXCLUDED.permissions,
              status = EXCLUDED.status
          `, [server.uuid, member.userId, member.role, member.permissions, member.status])
          synchronizedMembers += 1
        }
      }

      const synchronizedUuids = servers.map(server => server.uuid)
      await client.query(`
        UPDATE servers
        SET status = $2, last_synced_at = now()
        WHERE installation_id = $1
          AND NOT (uuid = ANY($3::text[]))
      `, [installation.id, 'Offline', synchronizedUuids])
      return { servers: servers.length, members: synchronizedMembers }
    })

    return json({ status: 'ok', synchronized: summary })
  } catch (caught) {
    if (caught instanceof HttpError) throw caught
    if (caught?.code === '23505') throw new HttpError(409, 'Server UUID or subdomain is already reserved.')
    throw caught
  }
})
