import { requireSession } from '../lib/auth.mjs'
import { query } from '../lib/db.mjs'
import { json, requireMethod, route } from '../lib/http.mjs'

const SUBDOMAIN_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/

function normalizeSubdomain(value) {
  if (typeof value !== 'string') return null
  const normalized = value.trim().toLowerCase()
  return SUBDOMAIN_PATTERN.test(normalized) ? normalized : null
}

function publicServer(row) {
  const subdomain = normalizeSubdomain(row.subdomain)
  const permissions = row.permissions && typeof row.permissions === 'object' && !Array.isArray(row.permissions)
    ? row.permissions
    : {}
  return {
    uuid: row.uuid,
    name: row.name,
    software_type: row.software_type,
    version: row.version,
    status: row.status,
    port: row.port,
    ram_min: row.ram_min,
    ram_max: row.ram_max,
    subdomain,
    public_url: subdomain && row.subdomain_state === 'published' ? `https://${subdomain}.mcdeploy.online` : '',
    role: row.role,
    permissions,
    last_synced_at: row.last_synced_at
  }
}

export default route(async request => {
  requireMethod(request, 'GET')
  const user = await requireSession(request)
  const result = await query(`
    SELECT s.uuid, s.name, s.software_type, s.version, s.port,
           s.ram_min, s.ram_max, s.subdomain, s.last_synced_at,
           m.role, m.permissions, r.state AS subdomain_state,
           CASE
             WHEN i.status <> $2
              AND i.last_seen_at IS NOT NULL
              AND i.last_seen_at > now() - ($3 * interval '1 second')
             THEN s.status
             ELSE $4
           END AS status
    FROM server_members m
    JOIN servers s ON s.uuid = m.server_uuid
    JOIN installations i ON i.id = s.installation_id
    LEFT JOIN subdomain_reservations r ON r.server_uuid = s.uuid AND r.subdomain = s.subdomain
    WHERE m.user_id = $1
      AND m.status = $5
      AND m.permissions ->> $6 = $7
    ORDER BY lower(s.name), s.uuid
  `, [user.id, 'suspended', 90, 'Offline', 'active', 'server.view', 'true'])

  return json({ status: 'success', servers: result.rows.map(publicServer) })
})

export const config = { path: '/api/webpanel/servers' }
