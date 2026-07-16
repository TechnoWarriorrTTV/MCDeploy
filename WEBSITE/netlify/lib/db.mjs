import { getDatabase } from '@netlify/database'

let pool

export function databasePool() {
  if (!pool) pool = getDatabase().pool
  if (!pool) throw new Error('Database is unavailable.')
  return pool
}

export function query(text, values = []) {
  return databasePool().query(text, values)
}

export async function transaction(work) {
  const client = await databasePool().connect()
  try {
    await client.query('BEGIN')
    const result = await work(client)
    await client.query('COMMIT')
    return result
  } catch (error) {
    await client.query('ROLLBACK')
    throw error
  } finally {
    client.release()
  }
}

export async function cleanupExpired(client = databasePool()) {
  await client.query('DELETE FROM pending_registrations WHERE expires_at < now()')
  await client.query('DELETE FROM sessions WHERE expires_at < now()')
  await client.query('DELETE FROM enrollment_codes WHERE expires_at < now()')
  await client.query("UPDATE agent_commands SET status = 'expired' WHERE status IN ('pending','claimed') AND expires_at <= now()")
  await client.query("DELETE FROM agent_commands WHERE status IN ('succeeded','failed','expired') AND created_at < now() - interval '30 days'")
  await client.query("DELETE FROM request_rate_limits WHERE updated_at < now() - interval '7 days'")
}
