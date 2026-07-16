import { AGENT_ACTIONS } from '../lib/agent-actions.mjs'
import { requireAgent } from '../lib/auth.mjs'
import { transaction } from '../lib/db.mjs'
import { json, requireMethod, route } from '../lib/http.mjs'

export const config = { path: '/api/agent/commands' }

const CLAIM_LIMIT = 10

export default route(async request => {
  requireMethod(request, 'GET')
  const installation = await requireAgent(request)

  const commands = await transaction(async client => {
    await client.query(`
      UPDATE agent_commands
      SET status = $2
      WHERE installation_id = $1
        AND status IN ($3, $4)
        AND expires_at <= now()
    `, [installation.id, 'expired', 'pending', 'claimed'])

    const claimed = await client.query(`
      WITH selected AS (
        SELECT id
        FROM agent_commands
        WHERE installation_id = $1
          AND status = $2
          AND expires_at > now()
          AND action = ANY($3::text[])
        ORDER BY created_at, id
        LIMIT $4
        FOR UPDATE SKIP LOCKED
      )
      UPDATE agent_commands AS command
      SET status = $5, claimed_at = now()
      FROM selected
      WHERE command.id = selected.id
      RETURNING command.id, command.server_uuid, command.action,
                command.payload, command.expires_at, command.created_at
    `, [installation.id, 'pending', AGENT_ACTIONS, CLAIM_LIMIT, 'claimed'])
    return claimed.rows.map(row => ({
      command_id: row.id,
      server_uuid: row.server_uuid,
      action: row.action,
      payload: row.payload,
      created_at: row.created_at,
      expires_at: row.expires_at
    }))
  })

  return json({ status: 'ok', commands })
})
