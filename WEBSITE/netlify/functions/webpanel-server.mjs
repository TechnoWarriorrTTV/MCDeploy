import { requireSession } from '../lib/auth.mjs'
import { query } from '../lib/db.mjs'
import { HttpError, json, requireMethod, route } from '../lib/http.mjs'

const SUBDOMAIN_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/
const SERVER_UUID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/
const METRIC_LIMITS = Object.freeze({
  cpu_usage: 100,
  ram_used_gb: 1_000_000,
  ram_total_gb: 1_000_000,
  disk_used_gb: 1_000_000_000,
  disk_total_gb: 1_000_000_000,
  world_size_gb: 1_000_000_000
})

function pathUuid(request) {
  const match = new URL(request.url).pathname.match(/^\/api\/webpanel\/servers\/([^/]+)\/?$/)
  if (!match) throw new HttpError(404, 'Route not found.')
  let uuid
  try { uuid = decodeURIComponent(match[1]) } catch { throw new HttpError(400, 'Invalid server ID.') }
  if (!SERVER_UUID_PATTERN.test(uuid)) throw new HttpError(400, 'Invalid server ID.')
  return uuid
}

function normalizedSubdomain(value) {
  if (typeof value !== 'string') return null
  const normalized = value.trim().toLowerCase()
  return SUBDOMAIN_PATTERN.test(normalized) ? normalized : null
}

function safeHostMetrics(metadata) {
  const source = metadata?.host_metrics
  if (!source || typeof source !== 'object' || Array.isArray(source)) return null
  const output = {}
  for (const [key, maximum] of Object.entries(METRIC_LIMITS)) {
    const value = source[key]
    if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 || value > maximum) return null
    output[key] = value
  }
  if (output.ram_used_gb > output.ram_total_gb || output.disk_used_gb > output.disk_total_gb) return null
  return output
}

function publicServer(row) {
  const subdomain = normalizedSubdomain(row.subdomain)
  return {
    uuid: row.uuid,
    name: row.name,
    software_type: row.software_type,
    version: row.version,
    status: row.effective_status,
    port: row.port,
    ram_min: row.ram_min,
    ram_max: row.ram_max,
    subdomain,
    public_url: subdomain && row.subdomain_state === 'published' ? `https://${subdomain}.mcdeploy.online` : '',
    role: row.role,
    permissions: row.permissions && typeof row.permissions === 'object' && !Array.isArray(row.permissions)
      ? row.permissions
      : {},
    last_synced_at: row.last_synced_at
  }
}

export default route(async request => {
  requireMethod(request, 'GET')
  const user = await requireSession(request)
  const uuid = pathUuid(request)
  const result = await query(`
    SELECT s.uuid, s.name, s.software_type, s.version, s.port,
           s.ram_min, s.ram_max, s.subdomain, s.metadata, s.last_synced_at,
           m.role, m.permissions, r.state AS subdomain_state,
           CASE
             WHEN i.status <> $3
              AND i.last_seen_at IS NOT NULL
              AND i.last_seen_at > now() - make_interval(secs => $4)
              AND s.last_synced_at > now() - make_interval(secs => $4)
             THEN s.status
             ELSE $5
           END AS effective_status,
           CASE
             WHEN i.status <> $3
              AND i.last_seen_at IS NOT NULL
              AND i.last_seen_at > now() - make_interval(secs => $4)
              AND s.last_synced_at > now() - make_interval(secs => $4)
             THEN true
             ELSE false
           END AS synchronized
    FROM servers s
    JOIN installations i ON i.id = s.installation_id
    LEFT JOIN subdomain_reservations r ON r.server_uuid = s.uuid AND r.subdomain = s.subdomain
    JOIN server_members m ON m.server_uuid = s.uuid
    WHERE m.user_id = $1
      AND s.uuid = $2
      AND m.status = $6
      AND m.permissions ->> $7 = $8
  `, [user.id, uuid, 'suspended', 90, 'Offline', 'active', 'server.view', 'true'])
  const row = result.rows[0]
  if (!row) throw new HttpError(404, 'Server not found.')

  const response = { status: 'success', server: publicServer(row) }
  const metrics = row.synchronized ? safeHostMetrics(row.metadata) : null
  if (metrics) response.host_metrics = metrics
  return json(response)
})

export const config = { path: '/api/webpanel/servers/:uuid' }
