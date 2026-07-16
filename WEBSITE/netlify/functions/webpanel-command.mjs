import { requireSession } from '../lib/auth.mjs'
import { commandStatus, queueCommand } from '../lib/commands.mjs'
import { query } from '../lib/db.mjs'
import { hasPermission, requireMembership } from '../lib/permissions.mjs'
import { HttpError, json, readJson, route } from '../lib/http.mjs'

const SOURCE = 'modrinth'
const SERVER_UUID = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/
const ADDON_ID = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/
const TOOL_NAME = /^[A-Za-z][A-Za-z0-9_.:-]{0,99}$/
const SENSITIVE_CONFIG = /(password|secret|token|api[-_]?key)|^rcon\./i
const CPU_PRIORITIES = new Set(['low', 'below_normal', 'normal', 'above_normal', 'high'])

function plainObject(value, name) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.getPrototypeOf(value) !== Object.prototype) {
    throw new HttpError(400, `${name} must be an object.`)
  }
  return value
}

function textValue(value, name, maximum, { multiline = false, required = true } = {}) {
  if (typeof value !== 'string') throw new HttpError(400, `${name} must be a string.`)
  const normalized = value.trim()
  const controls = multiline
    ? /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/
    : /[\x00-\x1f\x7f]/
  if ((required && !normalized) || normalized.length > maximum || controls.test(normalized)) {
    throw new HttpError(400, `Invalid ${name}.`)
  }
  return normalized
}

function boundedObject(value, name, maximumBytes) {
  plainObject(value, name)
  let serialized
  try { serialized = JSON.stringify(value) } catch { throw new HttpError(400, `Invalid ${name}.`) }
  if (Buffer.byteLength(serialized, 'utf8') > maximumBytes) throw new HttpError(413, `${name} is too large.`)
  return value
}

function onlyQuery(url, allowed) {
  for (const key of url.searchParams.keys()) {
    if (!allowed.includes(key) || url.searchParams.getAll(key).length !== 1) {
      throw new HttpError(400, 'Unsupported query parameters.')
    }
  }
}

function noQuery(url) {
  onlyQuery(url, [])
}

function noBody(request) {
  if (request.body !== null) throw new HttpError(400, 'This operation does not accept a request body.')
}

function safeRelativePath(value, { required = false } = {}) {
  if (typeof value !== 'string') throw new HttpError(400, 'path must be a string.')
  if ((required && !value) || value.length > 1024 || /[\x00-\x1f\x7f]/.test(value)) {
    throw new HttpError(400, 'Invalid path.')
  }
  if (/^(?:[a-z]:[\\/]|[\\/]{1,2}|~(?:[\\/]|$)|[a-z][a-z0-9+.-]*:)/i.test(value)) {
    throw new HttpError(400, 'Absolute paths are not allowed.')
  }
  if (value.split(/[\\/]/).some(part => part === '..')) throw new HttpError(400, 'Path traversal is not allowed.')
  return value
}

function safeFilename(value, { jar = false } = {}) {
  const filename = textValue(value, 'filename', 255)
  if (filename === '.' || filename === '..' || /[\\/<>:"|?*]/.test(filename) || /[. ]$/.test(filename)) {
    throw new HttpError(400, 'Invalid filename.')
  }
  if (jar && !filename.toLowerCase().endsWith('.jar')) throw new HttpError(400, 'A safe .jar filename is required.')
  return filename
}

function decodeSegment(value, label) {
  let decoded
  try { decoded = decodeURIComponent(value) } catch { throw new HttpError(400, `Invalid ${label}.`) }
  return decoded
}

function parseRoute(url) {
  const command = url.pathname.match(/^\/api\/webpanel\/commands\/([^/]+)\/?$/)
  if (command) return { type: 'command', commandId: decodeSegment(command[1], 'command ID') }
  const server = url.pathname.match(/^\/api\/webpanel\/servers\/([^/]+)\/(.+?)\/?$/)
  if (!server) throw new HttpError(404, 'Route not found.')
  const uuid = decodeSegment(server[1], 'server ID')
  if (!SERVER_UUID.test(uuid)) throw new HttpError(400, 'Invalid server ID.')
  return { type: 'server', uuid, suffix: server[2] }
}

function queryValue(url, name, { required = false, maximum = 1024 } = {}) {
  const value = url.searchParams.get(name)
  if (value === null) {
    if (required) throw new HttpError(400, `${name} is required.`)
    return ''
  }
  return textValue(value, name, maximum, { required })
}

function validateProperties(properties) {
  boundedObject(properties, 'properties', 65536)
  const entries = Object.entries(properties)
  if (entries.length > 200) throw new HttpError(400, 'Too many configuration properties.')
  const safe = Object.create(null)
  for (const [key, value] of entries) {
    if (!key || key.length > 128 || /[=\r\n\x00-\x1f\x7f]/.test(key)) throw new HttpError(400, 'Invalid configuration property name.')
    if (SENSITIVE_CONFIG.test(key) || value === '[REDACTED]') continue
    if (!['string', 'number', 'boolean'].includes(typeof value) || (typeof value === 'number' && !Number.isFinite(value))) {
      throw new HttpError(400, 'Configuration property values must be strings, numbers, or booleans.')
    }
    if (typeof value === 'string' && value.length > 4096) throw new HttpError(400, 'A configuration property value is too large.')
    safe[key] = value
  }
  return safe
}

function validateArgumentPaths(value, key = '', depth = 0) {
  if (depth > 8) throw new HttpError(400, 'arguments is too complex.')
  if (Array.isArray(value)) {
    for (const item of value) validateArgumentPaths(item, key, depth + 1)
    return
  }
  if (!value || typeof value !== 'object') {
    if (typeof value === 'string' && /(?:^|_)(?:path|directory)$/i.test(key)) safeRelativePath(value, { required: true })
    if (typeof value === 'string' && /(?:^|_)(?:file|filename)$/i.test(key)) safeFilename(value)
    return
  }
  for (const [childKey, child] of Object.entries(value)) validateArgumentPaths(child, childKey, depth + 1)
}

function trustedDownload(value) {
  const raw = textValue(value, 'downloadUrl', 2048)
  let parsed
  try { parsed = new URL(raw) } catch { throw new HttpError(400, 'Invalid downloadUrl.') }
  const hostname = parsed.hostname.toLowerCase()
  if (parsed.protocol !== 'https:' || parsed.username || parsed.password ||
      (hostname !== 'cdn.modrinth.com' && !hostname.endsWith('.forgecdn.net'))) {
    throw new HttpError(400, 'Downloads must use an approved add-on CDN.')
  }
  return parsed.toString()
}

async function emptyPayload(request, url) {
  noQuery(url)
  noBody(request)
  return {}
}

const ROUTES = {
  logs: {
    GET: { permission: 'console.view', action: 'server.logs.read', payload: emptyPayload }
  },
  backups: {
    GET: { permission: 'backups.view', action: 'backup.list', payload: emptyPayload },
    POST: { permission: 'backups.create', action: 'backup.create', payload: emptyPayload }
  },
  health: {
    GET: { permission: 'metrics.view', action: 'health.read', payload: emptyPayload }
  },
  config: {
    GET: { permission: 'config.view', action: 'config.read', payload: emptyPayload },
    PUT: {
      permission: 'config.edit', action: 'config.update', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 70000, fields: ['properties'] })
        return { properties: validateProperties(body.properties) }
      }
    }
  },
  files: {
    GET: {
      permission: 'files.view', action: 'files.list', payload: async (request, url) => {
        noBody(request)
        onlyQuery(url, ['path'])
        return { path: safeRelativePath(url.searchParams.get('path') || '') }
      }
    }
  },
  file: {
    GET: {
      permission: 'files.read', action: 'files.read', payload: async (request, url) => {
        noBody(request)
        onlyQuery(url, ['path'])
        return { path: safeRelativePath(queryValue(url, 'path', { required: true }), { required: true }) }
      }
    },
    PUT: {
      permission: 'files.edit', action: 'files.write', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 1_100_000, fields: ['path', 'content'] })
        const path = safeRelativePath(body.path, { required: true })
        if (typeof body.content !== 'string' || Buffer.byteLength(body.content, 'utf8') > 1_048_576) {
          throw new HttpError(400, 'File content must be a string no larger than 1 MB.')
        }
        return { path, content: body.content }
      }
    }
  },
  players: {
    GET: { permission: 'players.view', action: 'players.list', payload: emptyPayload }
  },
  analytics: {
    GET: {
      permission: 'analytics.view', action: 'analytics.read', payload: async (request, url) => {
        noBody(request)
        onlyQuery(url, ['days'])
        const raw = url.searchParams.get('days') || '30'
        if (!/^\d{1,3}$/.test(raw)) throw new HttpError(400, 'days must be an integer.')
        const days = Number(raw)
        if (days < 1 || days > 365) throw new HttpError(400, 'days must be between 1 and 365.')
        return { days }
      }
    }
  },
  schedule: {
    GET: { permission: 'schedule.view', action: 'schedule.read', payload: emptyPayload }
  },
  performance: {
    GET: { permission: 'metrics.view', action: 'performance.read', payload: emptyPayload },
    PUT: {
      permission: 'config.performance', action: 'performance.update', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 2048, fields: ['cpu_priority', 'smart_optimization'] })
        if (!Object.keys(body).length) throw new HttpError(400, 'At least one performance setting is required.')
        const payload = {}
        if ('cpu_priority' in body) {
          if (typeof body.cpu_priority !== 'string' || !CPU_PRIORITIES.has(body.cpu_priority)) throw new HttpError(400, 'Invalid CPU priority.')
          payload.cpu_priority = body.cpu_priority
        }
        if ('smart_optimization' in body) {
          if (typeof body.smart_optimization !== 'boolean') throw new HttpError(400, 'smart_optimization must be a boolean.')
          payload.smart_optimization = body.smart_optimization
        }
        return payload
      }
    }
  },
  automation: {
    GET: { permission: 'automation.view', action: 'automation.read', payload: emptyPayload }
  },
  maintenance: {
    GET: { permission: 'maintenance.view', action: 'maintenance.read', payload: emptyPayload },
    PUT: {
      permission: 'maintenance.manage', action: 'maintenance.update', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, {
          maxBytes: 4096,
          fields: ['enabled', 'message', 'prevent_joins', 'backup_on_enable']
        })
        if (!Object.keys(body).length) throw new HttpError(400, 'At least one maintenance setting is required.')
        const payload = {}
        for (const key of ['enabled', 'prevent_joins', 'backup_on_enable']) {
          if (key in body) {
            if (typeof body[key] !== 'boolean') throw new HttpError(400, `${key} must be a boolean.`)
            payload[key] = body[key]
          }
        }
        if ('message' in body) payload.message = textValue(body.message, 'message', 512, { required: false })
        return payload
      }
    }
  },
  plugins: {
    GET: { permission: 'plugins.view', action: 'plugins.list', payload: emptyPayload }
  },
  'plugins/search': {
    GET: {
      permission: 'plugins.view', action: 'plugins.search', payload: async (request, url) => {
        noBody(request)
        onlyQuery(url, ['source', 'query'])
        const source = queryValue(url, 'source', { required: true, maximum: 32 })
        if (source !== SOURCE) throw new HttpError(400, 'Unsupported add-on source.')
        return { source, query: queryValue(url, 'query', { required: true, maximum: 100 }) }
      }
    }
  },
  'plugins/install': {
    POST: {
      permission: 'plugins.install', action: 'plugins.install', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 8192, fields: ['downloadUrl', 'filename', 'addonName'] })
        return {
          downloadUrl: trustedDownload(body.downloadUrl),
          filename: safeFilename(body.filename, { jar: true }),
          addonName: textValue(body.addonName ?? body.filename, 'addonName', 200)
        }
      }
    }
  },
  'plugins/uninstall': {
    DELETE: {
      permission: 'plugins.uninstall', action: 'plugins.uninstall', payload: async (request, url) => {
        noBody(request)
        onlyQuery(url, ['filename'])
        return { filename: safeFilename(queryValue(url, 'filename', { required: true, maximum: 255 })) }
      }
    }
  },
  'ai/conversation': {
    GET: { permission: 'ai.use', action: 'ai.conversation.read', payload: emptyPayload },
    DELETE: { permission: 'ai.use', action: 'ai.conversation.clear', payload: emptyPayload }
  },
  'ai/chat': {
    POST: {
      permission: 'ai.use', action: 'ai.chat', payload: async (request, url, membership) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 16384, fields: ['message', 'agent_mode'] })
        if ('agent_mode' in body && typeof body.agent_mode !== 'boolean') throw new HttpError(400, 'agent_mode must be a boolean.')
        const agentMode = body.agent_mode === true
        if (agentMode && !hasPermission(membership, 'ai.agent_mode')) throw new HttpError(403, 'Permission required: ai.agent_mode.')
        return { message: textValue(body.message, 'message', 12000, { multiline: true }), agent_mode: agentMode }
      }
    }
  },
  'ai/approve': {
    POST: {
      permission: 'ai.approve', action: 'ai.approve', payload: async (request, url, membership) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 32768, fields: ['tool', 'arguments'] })
        const tool = textValue(body.tool, 'tool', 100)
        if (!TOOL_NAME.test(tool)) throw new HttpError(400, 'Invalid tool.')
        if (tool === 'run_shell_command' && !hasPermission(membership, 'ai.shell')) throw new HttpError(403, 'Permission required: ai.shell.')
        const argumentsValue = boundedObject(body.arguments ?? {}, 'arguments', 24576)
        validateArgumentPaths(argumentsValue)
        return { tool, arguments: argumentsValue }
      }
    }
  },
  'ai/undo': {
    POST: { permission: 'ai.undo', action: 'ai.undo', payload: emptyPayload }
  },
  control: {
    POST: {
      permission: null, action: null, lifecycle: true, payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 1024, fields: ['action'] })
        if (!['start', 'stop', 'restart'].includes(body.action)) throw new HttpError(400, 'Invalid server action.')
        return { payload: { action: body.action }, permission: `server.${body.action}`, action: `server.${body.action}` }
      }
    }
  },
  command: {
    POST: {
      permission: 'console.send', action: 'console.send', payload: async (request, url) => {
        noQuery(url)
        const body = await readJson(request, { maxBytes: 4096, fields: ['command'] })
        return { command: textValue(body.command, 'command', 2048) }
      }
    }
  }
}

function pluginVersionsRoute(suffix) {
  const match = suffix.match(/^plugins\/([^/]+)\/versions$/)
  if (!match) return null
  const addon = decodeSegment(match[1], 'add-on ID')
  if (!ADDON_ID.test(addon)) throw new HttpError(400, 'Invalid add-on ID.')
  return {
    permission: 'plugins.view',
    action: 'plugins.versions',
    payload: async (request, url) => {
      noBody(request)
      onlyQuery(url, ['source'])
      const source = queryValue(url, 'source', { required: true, maximum: 32 })
      if (source !== SOURCE) throw new HttpError(400, 'Unsupported add-on source.')
      return { source, addon }
    }
  }
}

async function auditResponse(userId, uuid, request, url) {
  noBody(request)
  noQuery(url)
  await requireMembership(userId, uuid, 'audit.view')
  const result = await query(`
    SELECT id, action, details, created_at
    FROM audit_logs
    WHERE server_uuid = $1
    ORDER BY created_at DESC, id DESC
    LIMIT $2
  `, [uuid, 200])
  return json({ entries: result.rows })
}

export default route(async request => {
  const url = new URL(request.url)
  const user = await requireSession(request)
  const parsed = parseRoute(url)

  if (parsed.type === 'command') {
    if (request.method !== 'GET') throw new HttpError(405, 'Method not allowed.')
    noBody(request)
    noQuery(url)
    return json(await commandStatus(user.id, parsed.commandId))
  }

  if (parsed.suffix === 'audit') {
    if (request.method !== 'GET') throw new HttpError(405, 'Method not allowed.')
    return auditResponse(user.id, parsed.uuid, request, url)
  }

  const methods = ROUTES[parsed.suffix]
  let descriptor = methods?.[request.method]
  if (!descriptor) {
    const versions = pluginVersionsRoute(parsed.suffix)
    if (versions) {
      if (request.method !== 'GET') throw new HttpError(405, 'Method not allowed.')
      descriptor = versions
    } else if (methods) {
      throw new HttpError(405, 'Method not allowed.')
    } else {
      throw new HttpError(404, 'Route not found.')
    }
  }

  let membership
  let permission = descriptor.permission
  let action = descriptor.action
  if (parsed.suffix === 'control') {
    const resolved = await descriptor.payload(request, url)
    permission = resolved.permission
    action = resolved.action
    membership = await requireMembership(user.id, parsed.uuid, permission)
    const accepted = await queueCommand({
      request, userId: user.id, membership, action,
      payload: resolved.payload, lifecycle: true
    })
    return json(accepted, 202)
  }

  membership = await requireMembership(user.id, parsed.uuid, permission)
  const payload = await descriptor.payload(request, url, membership)
  const accepted = await queueCommand({
    request, userId: user.id, membership, action, payload,
    lifecycle: descriptor.lifecycle === true
  })
  return json(accepted, 202)
})

export const config = {
  path: ['/api/webpanel/commands/:id', '/api/webpanel/servers/:uuid/*']
}
