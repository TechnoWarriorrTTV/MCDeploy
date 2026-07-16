import { AGENT_ACTIONS } from '../lib/agent-actions.mjs'
import { requireAgent } from '../lib/auth.mjs'
import { json, requireMethod, route } from '../lib/http.mjs'

export const config = { path: '/api/agent/heartbeat' }

export default route(async request => {
  requireMethod(request, 'POST')
  const installation = await requireAgent(request)
  return json({
    status: 'online',
    installation_id: installation.id,
    capabilities: {
      command_polling: true,
      result_reporting: true,
      inventory_sync: true,
      native_execution: false,
      dns_management: false,
      approved_actions: AGENT_ACTIONS
    }
  })
})
