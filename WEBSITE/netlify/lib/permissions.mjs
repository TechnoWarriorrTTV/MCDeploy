import { query } from './db.mjs'
import { HttpError } from './http.mjs'

export async function requireMembership(userId, serverUuid, permission) {
  const result = await query(`
    SELECT s.*, i.owner_user_id, i.status AS installation_status,
           m.role, m.permissions
    FROM servers s
    JOIN installations i ON i.id = s.installation_id
    JOIN server_members m ON m.server_uuid = s.uuid AND m.user_id = $1
    WHERE s.uuid = $2 AND m.status = 'active'
  `, [userId, serverUuid])
  const membership = result.rows[0]
  if (!membership) throw new HttpError(404, 'Server not found.')
  if (permission && membership.permissions?.[permission] !== true) throw new HttpError(403, `Permission required: ${permission}.`)
  return membership
}

export function hasPermission(membership, permission) {
  return membership?.permissions?.[permission] === true
}
