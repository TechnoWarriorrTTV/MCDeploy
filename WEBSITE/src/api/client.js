const configuredOrigin = (import.meta.env.VITE_MCDEPLOY_API_URL || '').trim()
export const API_ORIGIN = configuredOrigin.replace(/\/+$/, '')

const COMMAND_POLL_DELAY_MS = 1000
const COMMAND_POLL_MAX_MS = 120000
const COMMAND_STATUS_TIMEOUT_MS = 7000

export class ApiError extends Error {
  constructor(message, status = 0, disconnected = false) {
    super(message)
    this.name = 'ApiError'
    this.status = status
    this.disconnected = disconnected
  }
}

function createIdempotencyKey() {
  if (typeof globalThis.crypto.randomUUID === 'function') return globalThis.crypto.randomUUID()

  const bytes = new Uint8Array(16)
  globalThis.crypto.getRandomValues(bytes)
  bytes[6] = (bytes[6] & 0x0f) | 0x40
  bytes[8] = (bytes[8] & 0x3f) | 0x80
  const value = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('')
  return `${value.slice(0, 8)}-${value.slice(8, 12)}-${value.slice(12, 16)}-${value.slice(16, 20)}-${value.slice(20)}`
}

function isMutatingServerRequest(path, method) {
  return !['GET', 'HEAD', 'OPTIONS'].includes(method) && /^\/webpanel\/servers\/[^/?]+(?:[/?]|$)/.test(path)
}

function expiresInMilliseconds(data) {
  const seconds = Number(data?.expires_in)
  return Number.isFinite(seconds) && seconds >= 0 ? seconds * 1000 : null
}

function commandErrorMessage(data, fallback) {
  return data?.result?.message || data?.message || data?.error || fallback
}

function wait(milliseconds) {
  return new Promise(resolve => window.setTimeout(resolve, milliseconds))
}

async function pollCommand(command, token) {
  const commandId = encodeURIComponent(command.command_id)
  const serverExpiry = expiresInMilliseconds(command)
  let deadline = Date.now() + Math.min(serverExpiry ?? COMMAND_POLL_MAX_MS, COMMAND_POLL_MAX_MS)

  while (Date.now() < deadline) {
    await wait(Math.min(COMMAND_POLL_DELAY_MS, Math.max(0, deadline - Date.now())))
    if (Date.now() >= deadline) break

    let status
    try {
      status = await request(`/webpanel/commands/${commandId}`, {
        token,
        timeout: Math.max(1, Math.min(COMMAND_STATUS_TIMEOUT_MS, deadline - Date.now())),
        handleAccepted: false
      })
    } catch (error) {
      if (!(error instanceof ApiError) || !error.disconnected || Date.now() >= deadline) throw error
      continue
    }

    if (status.status === 'succeeded') {
      return status.result && typeof status.result === 'object' ? status.result : {}
    }
    if (status.status === 'failed') {
      throw new ApiError(commandErrorMessage(status, 'The MCDeploy agent could not complete the request.'), 409)
    }
    if (status.status === 'expired') {
      throw new ApiError(commandErrorMessage(status, 'The MCDeploy command expired before it completed.'), 408)
    }
    if (!['pending', 'claimed'].includes(status.status)) {
      throw new ApiError('The MCDeploy cloud service returned an invalid command status.', 502)
    }

    const remaining = expiresInMilliseconds(status)
    if (remaining !== null) deadline = Math.min(deadline, Date.now() + remaining)
  }

  throw new ApiError('The MCDeploy command expired before it completed.', 408)
}

async function request(path, { token, timeout = 7000, handleAccepted = true, ...options } = {}) {
  const controller = new AbortController()
  const timer = window.setTimeout(() => controller.abort(), timeout)
  const method = String(options.method || 'GET').toUpperCase()
  const headers = new Headers(options.headers || {})
  if (options.body && !headers.has('Content-Type')) headers.set('Content-Type', 'application/json')
  if (token) headers.set('Authorization', `Bearer ${token}`)
  if (isMutatingServerRequest(path, method) && !headers.has('Idempotency-Key')) {
    headers.set('Idempotency-Key', createIdempotencyKey())
  }

  try {
    const response = await fetch(`${API_ORIGIN}/api${path}`, {
      ...options,
      method,
      signal: controller.signal,
      headers
    })
    let data = {}
    try { data = await response.json() } catch { /* Empty or non-JSON response. */ }
    if (!response.ok) {
      if (response.status === 404 && path.startsWith('/webpanel/')) {
        throw new ApiError('The MCDeploy account service is not connected to this website yet. Please try again after the service is deployed.', 404)
      }
      throw new ApiError(data.message || `Request failed (${response.status})`, response.status)
    }
    if (handleAccepted && response.status === 202 && data.command_id) return pollCommand(data, token)
    return data
  } catch (error) {
    if (error instanceof ApiError) throw error
    if (controller.signal.aborted) throw new ApiError('The MCDeploy cloud service request timed out.', 408, true)
    throw new ApiError('MCDeploy cloud service is temporarily unavailable.', 0, true)
  } finally {
    window.clearTimeout(timer)
  }
}

export const webpanelApi = {
  status: () => request('/webpanel/status'),
  register: (email, password, displayName) => request('/webpanel/register', {
    method: 'POST', body: JSON.stringify({ email: email.toLowerCase(), password, display_name: displayName })
  }),
  verifyRegistration: (email, code) => request('/webpanel/register/verify', {
    method: 'POST', body: JSON.stringify({ email: email.toLowerCase(), code })
  }),
  resendVerification: email => request('/webpanel/register/resend', {
    method: 'POST', body: JSON.stringify({ email: email.toLowerCase() })
  }),
  login: (email, password) => request('/webpanel/login', {
    method: 'POST', body: JSON.stringify({ email: email.toLowerCase(), password })
  }),
  oauthStartUrl: provider => `${API_ORIGIN}/api/webpanel/oauth/${encodeURIComponent(provider)}/start`,
  oauthExchange: code => request('/webpanel/oauth/exchange', {
    method: 'POST', body: JSON.stringify({ code })
  }),
  session: token => request('/webpanel/session', { token }),
  createEnrollmentCode: (token, installationName) => request('/agent/enrollment-code', {
    token, method: 'POST', body: JSON.stringify({ installation_name: installationName })
  }),
  servers: token => request('/webpanel/servers', { token }),
  overview: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}`, { token }),
  logs: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/logs`, { token }),
  backups: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/backups`, { token }),
  createBackup: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/backups`, { token, method: 'POST' }),
  health: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/health`, { token }),
  config: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/config`, { token }),
  updateConfig: (token, uuid, properties) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/config`, {
    token, method: 'PUT', body: JSON.stringify({ properties })
  }),
  files: (token, uuid, path = '') => request(`/webpanel/servers/${encodeURIComponent(uuid)}/files?path=${encodeURIComponent(path)}`, { token }),
  file: (token, uuid, path) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/file?path=${encodeURIComponent(path)}`, { token }),
  saveFile: (token, uuid, path, content) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/file`, {
    token, method: 'PUT', body: JSON.stringify({ path, content })
  }),
  players: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/players`, { token }),
  analytics: (token, uuid, days = 30) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/analytics?days=${days}`, { token }),
  schedule: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/schedule`, { token }),
  performance: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/performance`, { token }),
  updatePerformance: (token, uuid, settings) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/performance`, {
    token, method: 'PUT', body: JSON.stringify(settings)
  }),
  automation: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/automation`, { token }),
  maintenance: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/maintenance`, { token }),
  updateMaintenance: (token, uuid, maintenance) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/maintenance`, {
    token, method: 'PUT', body: JSON.stringify(maintenance)
  }),
  plugins: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/plugins`, { token }),
  searchPlugins: (token, uuid, query) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/plugins/search?source=modrinth&query=${encodeURIComponent(query)}`, { token, timeout: 15000 }),
  pluginVersions: (token, uuid, addon) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/plugins/${encodeURIComponent(addon)}/versions?source=modrinth`, { token, timeout: 15000 }),
  installPlugin: (token, uuid, addon) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/plugins/install`, {
    token, timeout: 30000, method: 'POST', body: JSON.stringify(addon)
  }),
  uninstallPlugin: (token, uuid, filename) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/plugins/uninstall?filename=${encodeURIComponent(filename)}`, { token, method: 'DELETE' }),
  aiConversation: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/ai/conversation`, { token }),
  clearAiConversation: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/ai/conversation`, { token, method: 'DELETE' }),
  aiChat: (token, uuid, message, agentMode) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/ai/chat`, {
    token, timeout: 120000, method: 'POST', body: JSON.stringify({ message, agent_mode: agentMode })
  }),
  aiApprove: (token, uuid, tool, argumentsValue) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/ai/approve`, {
    token, timeout: 30000, method: 'POST', body: JSON.stringify({ tool, arguments: argumentsValue })
  }),
  aiUndo: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/ai/undo`, { token, method: 'POST' }),
  audit: (token, uuid) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/audit`, { token }),
  control: (token, uuid, action) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/control`, {
    token, method: 'POST', body: JSON.stringify({ action })
  }),
  command: (token, uuid, command) => request(`/webpanel/servers/${encodeURIComponent(uuid)}/command`, {
    token, method: 'POST', body: JSON.stringify({ command })
  })
}