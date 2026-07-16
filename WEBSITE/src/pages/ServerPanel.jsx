import { useCallback, useEffect, useMemo, useState } from 'react'
import {
  Activity, ArrowLeft, BarChart3, Bot, Box, CalendarClock, ChevronLeft,
  DatabaseBackup, File, FileCode2, Folder, Gauge, HardDrive, HeartPulse, LoaderCircle,
  LockKeyhole, MemoryStick, Play, Plug, Power, RefreshCw, RotateCw, Save, Search,
  Send, Server, Settings2, ShieldCheck, TerminalSquare, Trash2, Users, Wrench
} from 'lucide-react'
import { Link, useParams } from 'react-router-dom'
import { webpanelApi } from '../api/client.js'
import { useAuth } from '../auth/useAuth.js'
import ConnectionNotice from '../components/ConnectionNotice.jsx'
import PanelHeader from '../components/PanelHeader.jsx'

const TABS = [
  ['overview', 'Overview', Server, 'server.view'],
  ['console', 'Console', TerminalSquare, 'console.view'],
  ['files', 'Files', Folder, 'files.view'],
  ['config', 'Config', Settings2, 'config.view'],
  ['players', 'Players', Users, 'players.view'],
  ['analytics', 'Analytics', BarChart3, 'analytics.view'],
  ['schedule', 'Schedule', CalendarClock, 'schedule.view'],
  ['backups', 'Backups', DatabaseBackup, 'backups.view'],
  ['performance', 'Performance', Gauge, 'metrics.view'],
  ['health', 'Health', HeartPulse, 'metrics.view'],
  ['automation', 'Automation', Activity, 'automation.view'],
  ['maintenance', 'Maintenance', Wrench, 'maintenance.view'],
  ['ai', 'AI Editor', Bot, 'ai.use'],
  ['plugins', 'Plugins / Mods', Plug, 'plugins.view'],
  ['audit', 'Audit', ShieldCheck, 'audit.view']
]

const formatBytes = value => {
  if (!value) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  const index = Math.min(Math.floor(Math.log(value) / Math.log(1024)), units.length - 1)
  return `${(value / (1024 ** index)).toFixed(index ? 1 : 0)} ${units[index]}`
}

function Metric({ icon: Icon, label, value, detail }) {
  return <article className="metric-card"><Icon size={19} /><span>{label}</span><strong>{value}</strong>{detail && <small>{detail}</small>}</article>
}
function LockedTab({ label, permission }) {
  return <section className="locked-workspace"><LockKeyhole size={32} /><h2>{label} is locked</h2><p>Your assigned role does not include <code>{permission}</code>. Ask the server owner to grant it from the MCDeploy Webpanel page.</p></section>
}

function DataTable({ rows = [], empty = 'No records found.' }) {
  if (!rows.length) return <div className="empty-state compact"><Box size={28} /><h2>{empty}</h2></div>
  const keys = Object.keys(rows[0]).filter(key => typeof rows[0][key] !== 'object').slice(0, 7)
  return <div className="data-table-wrap"><table className="data-table"><thead><tr>{keys.map(key => <th key={key}>{key.replaceAll('_', ' ')}</th>)}</tr></thead><tbody>{rows.map((row, index) => <tr key={row.id || row.uuid || index}>{keys.map(key => <td key={key}>{String(row[key] ?? '')}</td>)}</tr>)}</tbody></table></div>
}

export default function ServerPanel() {
  const { uuid } = useParams()
  const { token, signOut } = useAuth()
  const [overview, setOverview] = useState(null)
  const [tab, setTab] = useState('overview')
  const [data, setData] = useState({})
  const [loading, setLoading] = useState(true)
  const [tabLoading, setTabLoading] = useState(false)
  const [action, setAction] = useState('')
  const [connected, setConnected] = useState(false)
  const [error, setError] = useState('')
  const [command, setCommand] = useState('')
  const [currentPath, setCurrentPath] = useState('')
  const [openFile, setOpenFile] = useState(null)
  const [configValues, setConfigValues] = useState({})
  const [maintenanceForm, setMaintenanceForm] = useState({ enabled: false, message: '', prevent_joins: true, backup_on_enable: true })
  const [performanceForm, setPerformanceForm] = useState({ cpu_priority: 'normal', smart_optimization: false })
  const [aiMessage, setAiMessage] = useState('')
  const [agentMode, setAgentMode] = useState(false)
  const [pluginQuery, setPluginQuery] = useState('')
  const [pluginResults, setPluginResults] = useState([])
  const [pluginVersions, setPluginVersions] = useState({})

  const server = overview?.server
  const permissions = useMemo(() => server?.permissions || {}, [server])
  const can = useCallback(key => Boolean(permissions[key]), [permissions])
  const tabDefinition = TABS.find(([key]) => key === tab)
  const tabAllowed = tab === 'overview' || can(tabDefinition?.[3])

  const handleError = useCallback(requestError => {
    if (requestError.status === 401) return signOut()
    setConnected(!requestError.disconnected)
    setError(requestError.message)
  }, [signOut])

  const loadOverview = useCallback(async () => {
    try {
      const result = await webpanelApi.overview(token, uuid)
      setOverview(result)
      setConnected(true)
      setError('')
    } catch (requestError) { handleError(requestError) }
    finally { setLoading(false) }
  }, [token, uuid, handleError])

  const loadTab = useCallback(async (key = tab) => {
    const definition = TABS.find(([tabKey]) => tabKey === key)
    if (!definition || key === 'overview' || !can(definition[3])) return
    const loaders = {
      console: () => webpanelApi.logs(token, uuid),
      files: () => webpanelApi.files(token, uuid, currentPath),
      config: () => webpanelApi.config(token, uuid),
      players: () => webpanelApi.players(token, uuid),
      analytics: () => webpanelApi.analytics(token, uuid),
      schedule: () => webpanelApi.schedule(token, uuid),
      backups: () => webpanelApi.backups(token, uuid),
      performance: () => webpanelApi.performance(token, uuid),
      health: () => webpanelApi.health(token, uuid),
      automation: () => webpanelApi.automation(token, uuid),
      maintenance: () => webpanelApi.maintenance(token, uuid),
      ai: () => webpanelApi.aiConversation(token, uuid),
      plugins: () => webpanelApi.plugins(token, uuid),
      audit: () => webpanelApi.audit(token, uuid)
    }
    if (!loaders[key]) return
    setTabLoading(true)
    setError('')
    try {
      const result = await loaders[key]()
      setData(previous => ({ ...previous, [key]: result }))
      if (key === 'config') setConfigValues(result.properties || {})
      if (key === 'maintenance') setMaintenanceForm(result.maintenance || { enabled: false, message: '', prevent_joins: true, backup_on_enable: true })
      if (key === 'performance') setPerformanceForm({ cpu_priority: result.cpu_priority || 'normal', smart_optimization: Boolean(result.smart_optimization) })
      setConnected(true)
    } catch (requestError) { handleError(requestError) }
    finally { setTabLoading(false) }
  }, [tab, can, token, uuid, currentPath, handleError])

  useEffect(() => {
    const initial = window.setTimeout(loadOverview, 0)
    const refresh = window.setInterval(loadOverview, 7000)
    return () => { window.clearTimeout(initial); window.clearInterval(refresh) }
  }, [loadOverview])

  useEffect(() => {
    const initial = window.setTimeout(() => loadTab(tab), 0)
    let refresh
    if (tab === 'console' && tabAllowed) refresh = window.setInterval(() => loadTab('console'), 2000)
    return () => { window.clearTimeout(initial); if (refresh) window.clearInterval(refresh) }
  }, [tab, currentPath, tabAllowed, loadTab])

  const runAction = async (name, operation, reload = tab) => {
    setAction(name); setError('')
    try { await operation(); if (reload === 'overview') await loadOverview(); else await loadTab(reload) }
    catch (requestError) { handleError(requestError) }
    finally { setAction('') }
  }
  const sendCommand = event => {
    event.preventDefault()
    const value = command.trim()
    if (!value) return
    runAction('command', () => webpanelApi.command(token, uuid, value), 'console').then(() => setCommand(''))
  }

  const openPath = async entry => {
    if (entry.is_directory) { setOpenFile(null); setCurrentPath(entry.path); return }
    if (!can('files.read')) return setError('Opening files requires the files.read permission.')
    setAction('file-open'); setError('')
    try { setOpenFile(await webpanelApi.file(token, uuid, entry.path)) }
    catch (requestError) { handleError(requestError) }
    finally { setAction('') }
  }

  const parentPath = () => {
    const parts = currentPath.replaceAll('\\', '/').split('/').filter(Boolean)
    parts.pop(); setCurrentPath(parts.join('/')); setOpenFile(null)
  }

  const searchPlugins = async event => {
    event.preventDefault()
    if (!pluginQuery.trim()) return
    setAction('plugin-search'); setError('')
    try {
      const result = await webpanelApi.searchPlugins(token, uuid, pluginQuery.trim())
      setPluginResults(Array.isArray(result) ? result : result.results || [])
    } catch (requestError) { handleError(requestError) }
    finally { setAction('') }
  }

  const selectPlugin = async addon => {
    setAction(`versions-${addon.id}`); setError('')
    try {
      const result = await webpanelApi.pluginVersions(token, uuid, addon.id)
      const versions = Array.isArray(result) ? result : result.versions || []
      setPluginVersions(previous => ({ ...previous, [addon.id]: versions.slice(0, 8) }))
    } catch (requestError) { handleError(requestError) }
    finally { setAction('') }
  }

  const sendAi = async event => {
    event.preventDefault()
    const message = aiMessage.trim()
    if (!message) return
    setAction('ai-chat'); setError('')
    try {
      const result = await webpanelApi.aiChat(token, uuid, message, agentMode)
      setAiMessage('')
      setData(previous => ({ ...previous, ai: {
        conversation: [...(previous.ai?.conversation || []), { role: 'user', content: message }, { role: 'assistant', content: result.message }],
        pending_actions: result.pending_actions || []
      } }))
    } catch (requestError) { handleError(requestError) }
    finally { setAction('') }
  }

  if (loading) return <div className="panel-shell"><PanelHeader connected={false} /><div className="loading-state full"><LoaderCircle className="spin" /> Loading server access…</div></div>

  const renderOverview = () => <div className="tab-content">
    {overview.host_metrics && <div className="metrics-grid">
      <Metric icon={Server} label="Host CPU" value={`${Number(overview.host_metrics.cpu_usage).toFixed(1)}%`} detail="Current machine usage" />
      <Metric icon={MemoryStick} label="Host memory" value={`${Number(overview.host_metrics.ram_used_gb).toFixed(1)} GB`} detail={`of ${Number(overview.host_metrics.ram_total_gb).toFixed(1)} GB`} />
      <Metric icon={HardDrive} label="Host disk" value={`${Number(overview.host_metrics.disk_used_gb).toFixed(1)} GB`} detail={`of ${Number(overview.host_metrics.disk_total_gb).toFixed(1)} GB`} />
      <Metric icon={DatabaseBackup} label="World size" value={`${Number(overview.host_metrics.world_size_gb).toFixed(2)} GB`} detail="Measured server directory" />
    </div>}
    <section className="detail-card">
      <div><span>Server ID</span><strong>{server.uuid}</strong></div><div><span>Public address</span><strong>{server.public_url || 'Not configured'}</strong></div>
      <div><span>Memory allocation</span><strong>{server.ram_min}–{server.ram_max} MB</strong></div><div><span>Access role</span><strong className="capitalize">{server.role}</strong></div>
    </section>
  </div>

  const renderConsole = () => <section className="console-card">
    <div className="console-output" aria-live="polite">{data.console?.logs?.length ? data.console.logs.map(line => <div className={`log-line ${String(line.type).toLowerCase()}`} key={`${line.sequence}-${line.timestamp}`}><time>{line.timestamp}</time><b>{line.type}</b><span>{line.text}</span></div>) : <div className="console-empty">No console output recorded.</div>}</div>
    {can('console.send') && <form className="command-form" onSubmit={sendCommand}><span>&gt;</span><input value={command} onChange={event => setCommand(event.target.value)} placeholder="Enter a server command" /><button className="button button-primary" disabled={!command.trim() || action === 'command'}><Send size={15} /> Send</button></form>}
  </section>

  const renderFiles = () => <section className="workspace-card file-workspace">
    <header className="workspace-head"><div><h2>File manager</h2><p>/{currentPath}</p></div>{currentPath && <button className="button button-secondary" onClick={parentPath}><ChevronLeft size={16} /> Up</button>}</header>
    <div className="file-layout"><div className="file-list">{(data.files?.entries || []).map(entry => <button key={entry.path} onClick={() => openPath(entry)}>{entry.is_directory ? <Folder size={17} /> : <File size={17} />}<span>{entry.name}</span><small>{entry.is_directory ? 'Folder' : formatBytes(entry.size)}</small></button>)}</div>
      <div className="file-editor">{openFile ? <><div className="editor-title"><FileCode2 size={16} /><strong>{openFile.path}</strong>{can('files.edit') && !openFile.is_binary && <button className="button button-primary" onClick={() => runAction('file-save', () => webpanelApi.saveFile(token, uuid, openFile.path, openFile.content), 'files')} disabled={action === 'file-save'}><Save size={15} /> Save</button>}</div>{openFile.is_binary ? <div className="empty-state compact">Binary files cannot be edited.</div> : <textarea value={openFile.content} readOnly={!can('files.edit')} onChange={event => setOpenFile({ ...openFile, content: event.target.value })} />}</> : <div className="empty-state compact"><FileCode2 size={28} /><h2>Select a file</h2><p>Text files up to 512 KB can be previewed.</p></div>}</div>
    </div>
  </section>
  const renderConfig = () => <section className="workspace-card"><header className="workspace-head"><div><h2>Configuration manager</h2><p>Sensitive values are redacted and cannot be changed here.</p></div>{can('config.edit') && <button className="button button-primary" onClick={() => runAction('config-save', () => webpanelApi.updateConfig(token, uuid, configValues), 'config')}><Save size={15} /> Save changes</button>}</header><div className="config-grid">{Object.entries(configValues).map(([key, value]) => <label key={key}><span>{key}</span><input value={value} readOnly={!can('config.edit') || value === '[REDACTED]'} onChange={event => setConfigValues(previous => ({ ...previous, [key]: event.target.value }))} /></label>)}</div></section>

  const renderBackups = () => <section className="workspace-card"><header className="workspace-head"><div><h2>Backup archive</h2><p>Recorded safety snapshots for this server.</p></div>{can('backups.create') && <button className="button button-primary" onClick={() => runAction('backup-create', () => webpanelApi.createBackup(token, uuid), 'backups')}><DatabaseBackup size={16} /> Create backup</button>}</header><div className="backup-list">{data.backups?.backups?.length ? data.backups.backups.map(backup => <article key={backup.uuid}><DatabaseBackup size={19} /><div><strong>{backup.file_name}</strong><span>{backup.created_at}</span></div><b>{formatBytes(backup.file_size)}</b></article>) : <div className="empty-state compact"><DatabaseBackup size={30} /><h2>No backups recorded</h2></div>}</div></section>

  const renderPerformance = () => <section className="workspace-card"><header className="workspace-head"><div><h2>Performance controls</h2><p>Process tuning is applied by the MCDeploy host.</p></div>{can('config.performance') && <button className="button button-primary" onClick={() => runAction('performance-save', () => webpanelApi.updatePerformance(token, uuid, performanceForm), 'performance')}><Save size={15} /> Apply</button>}</header><div className="settings-grid"><label><span>CPU priority</span><select disabled={!can('config.performance')} value={performanceForm.cpu_priority} onChange={event => setPerformanceForm({ ...performanceForm, cpu_priority: event.target.value })}>{['low', 'below_normal', 'normal', 'above_normal', 'high'].map(value => <option key={value}>{value}</option>)}</select></label><label className="check-setting"><input type="checkbox" disabled={!can('config.performance')} checked={performanceForm.smart_optimization} onChange={event => setPerformanceForm({ ...performanceForm, smart_optimization: event.target.checked })} /><span>Smart optimization</span></label></div></section>

  const renderHealth = () => <div className="tab-content"><section className="health-score"><div><span>Server health</span><strong>{data.health?.score ?? '—'}</strong><b>/ 100 · {data.health?.grade || 'Loading'}</b></div><HeartPulse size={44} /></section><div className="component-grid">{(data.health?.components || []).map(component => <article key={component.key}><span>{component.key}</span><strong>{component.score}/{component.maximum}</strong><p>{component.evidence}</p></article>)}</div>{data.health?.recommendations?.length > 0 && <section className="workspace-card"><h2>Recommendations</h2><ul>{data.health.recommendations.map(item => <li key={item}>{item}</li>)}</ul></section>}</div>

  const renderMaintenance = () => <section className="workspace-card"><header className="workspace-head"><div><h2>Maintenance mode</h2><p>Announce downtime, take a safety backup, and control joins.</p></div>{can('maintenance.manage') && <button className="button button-primary" onClick={() => runAction('maintenance-save', () => webpanelApi.updateMaintenance(token, uuid, maintenanceForm), 'maintenance')}><Save size={15} /> Apply state</button>}</header><div className="settings-grid"><label><span>Player message</span><input disabled={!can('maintenance.manage')} value={maintenanceForm.message || ''} onChange={event => setMaintenanceForm({ ...maintenanceForm, message: event.target.value })} /></label>{[['enabled', 'Maintenance enabled'], ['prevent_joins', 'Prevent joins'], ['backup_on_enable', 'Backup before enabling']].map(([key, label]) => <label className="check-setting" key={key}><input type="checkbox" disabled={!can('maintenance.manage')} checked={Boolean(maintenanceForm[key])} onChange={event => setMaintenanceForm({ ...maintenanceForm, [key]: event.target.checked })} /><span>{label}</span></label>)}</div></section>

  const renderPlugins = () => <div className="tab-content"><section className="workspace-card"><header className="workspace-head"><div><h2>{data.plugins?.kind === 'mods' ? 'Mod' : 'Plugin'} installer</h2><p>Search verified Modrinth projects. Downloads are restricted to trusted provider CDNs.</p></div></header><form className="inline-form" onSubmit={searchPlugins}><div className="search-box"><Search size={16} /><input value={pluginQuery} onChange={event => setPluginQuery(event.target.value)} placeholder="Search Modrinth" /></div><button className="button button-primary" disabled={!pluginQuery.trim() || action === 'plugin-search'}><Search size={15} /> Search</button></form><div className="plugin-grid">{pluginResults.map(addon => <article key={addon.id}><div><h3>{addon.name}</h3><p>{addon.summary}</p></div><button className="button button-secondary" onClick={() => selectPlugin(addon)}>Choose version</button>{pluginVersions[addon.id]?.map(version => <div className="version-row" key={version.id}><span>{version.name}</span>{can('plugins.install') && <button className="button button-primary" disabled={!version.downloadUrl} onClick={() => runAction(`install-${version.id}`, () => webpanelApi.installPlugin(token, uuid, { downloadUrl: version.downloadUrl, filename: version.filename, addonName: addon.name }), 'plugins')}>Install</button>}</div>)}</article>)}</div></section><section className="workspace-card"><h2>Installed {data.plugins?.kind || 'add-ons'}</h2><div className="installed-list">{(data.plugins?.installed || []).map(item => <div key={item.filename}><Plug size={16} /><span>{item.filename}</span><small>{formatBytes(item.size)}</small>{can('plugins.uninstall') && <button className="icon-button danger" title="Uninstall" onClick={() => runAction(`remove-${item.filename}`, () => webpanelApi.uninstallPlugin(token, uuid, item.filename), 'plugins')}><Trash2 size={15} /></button>}</div>)}</div></section></div>
  const renderAi = () => <section className="workspace-card ai-workspace"><header className="workspace-head"><div><h2>AI Editor</h2><p>Conversation history is private to your webpanel email.</p></div><div className="workspace-actions">{can('ai.agent_mode') && <label className="agent-toggle"><input type="checkbox" checked={agentMode} onChange={event => setAgentMode(event.target.checked)} /> Agent Mode</label>}{can('ai.undo') && <button className="button button-secondary" onClick={() => runAction('ai-undo', () => webpanelApi.aiUndo(token, uuid), 'ai')}>Undo AI change</button>}<button className="button button-secondary" onClick={() => runAction('ai-clear', () => webpanelApi.clearAiConversation(token, uuid), 'ai')}>Clear</button></div></header><div className="ai-conversation">{(data.ai?.conversation || []).filter(message => message.content).map((message, index) => <article className={message.role} key={message.id || index}><b>{message.role === 'assistant' ? 'MCDeploy AI' : message.role}</b><p>{message.content}</p></article>)}</div>{(data.ai?.pending_actions || []).map((pending, index) => <div className="pending-action" key={pending.tool_call_id || index}><div><strong>Approval required: {pending.tool || pending.name}</strong><code>{JSON.stringify(pending.arguments || {})}</code></div>{can('ai.approve') && (pending.tool !== 'run_shell_command' || can('ai.shell')) && <button className="button button-danger" onClick={() => runAction('ai-approve', () => webpanelApi.aiApprove(token, uuid, pending.tool || pending.name, pending.arguments || {}), 'ai')}>Approve</button>}</div>)}<form className="ai-form" onSubmit={sendAi}><textarea value={aiMessage} onChange={event => setAiMessage(event.target.value)} placeholder="Ask about logs, configuration, files, or server health…" /><button className="button button-primary" disabled={!aiMessage.trim() || action === 'ai-chat'}>{action === 'ai-chat' ? <LoaderCircle className="spin" size={16} /> : <Send size={16} />} Send</button></form></section>

  const renderGeneric = () => {
    if (tab === 'players') return <section className="workspace-card"><header className="workspace-head"><div><h2>Players</h2><p>Live and historical player records.</p></div></header><DataTable rows={data.players?.players || []} empty="No players recorded." /></section>
    if (tab === 'analytics') return <section className="workspace-card"><header className="workspace-head"><div><h2>Analytics</h2><p>Thirty-day activity overview.</p></div></header><div className="json-grid"><article><h3>Summary</h3><pre>{JSON.stringify(data.analytics?.summary || {}, null, 2)}</pre></article><article><h3>Leaderboard</h3><pre>{JSON.stringify(data.analytics?.leaderboard || [], null, 2)}</pre></article></div></section>
    if (tab === 'schedule') return <section className="workspace-card"><header className="workspace-head"><div><h2>Schedule</h2><p>Scheduled tasks are read-only in the member webpanel.</p></div></header><DataTable rows={data.schedule?.tasks || []} empty="No scheduled tasks." /></section>
    if (tab === 'automation') return <section className="workspace-card"><header className="workspace-head"><div><h2>Automation rules</h2><p>Rule management stays controlled by the native app.</p></div></header><DataTable rows={data.automation?.rules || []} empty="No automation rules." /></section>
    if (tab === 'audit') return <section className="workspace-card"><header className="workspace-head"><div><h2>Audit log</h2><p>Recent actions associated with this server.</p></div></header><DataTable rows={data.audit?.entries || []} empty="No audit events." /></section>
    return null
  }

  const renderTab = () => {
    if (!tabAllowed) return <LockedTab label={tabDefinition?.[1] || 'Workspace'} permission={tabDefinition?.[3]} />
    if (tabLoading && !data[tab]) return <div className="loading-state"><LoaderCircle className="spin" /> Loading {tabDefinition?.[1]}…</div>
    if (tab === 'overview') return renderOverview()
    if (tab === 'console') return renderConsole()
    if (tab === 'files') return renderFiles()
    if (tab === 'config') return renderConfig()
    if (tab === 'backups') return renderBackups()
    if (tab === 'performance') return renderPerformance()
    if (tab === 'health') return renderHealth()
    if (tab === 'maintenance') return renderMaintenance()
    if (tab === 'plugins') return renderPlugins()
    if (tab === 'ai') return renderAi()
    return renderGeneric()
  }

  return <div className="panel-shell"><PanelHeader connected={connected} /><main className="panel-main server-panel"><Link className="back-link inline" to="/panel"><ArrowLeft size={16} /> All servers</Link>{!connected && <ConnectionNotice retry={loadOverview} />}{error && <div className="form-error" role="alert">{error}</div>}{server && <><section className="server-hero-card"><div className="server-identity"><span className="server-avatar large"><Server size={25} /></span><div><div className="server-title-line"><h1>{server.name}</h1><span className={`state-pill ${server.status === 'Online' ? 'online' : 'offline'}`}><i />{server.status}</span></div><p>{server.software_type} {server.version} · Port {server.port} · {server.role}</p></div></div><div className="control-row">{can('server.start') && server.status !== 'Online' && <button className="button button-primary" onClick={() => runAction('start', () => webpanelApi.control(token, uuid, 'start'), 'overview')} disabled={Boolean(action)}><Play size={16} /> Start</button>}{can('server.restart') && server.status === 'Online' && <button className="button button-secondary" onClick={() => runAction('restart', () => webpanelApi.control(token, uuid, 'restart'), 'overview')} disabled={Boolean(action)}><RotateCw size={16} /> Restart</button>}{can('server.stop') && server.status === 'Online' && <button className="button button-danger" onClick={() => runAction('stop', () => webpanelApi.control(token, uuid, 'stop'), 'overview')} disabled={Boolean(action)}><Power size={16} /> Stop</button>}<button className="icon-button" onClick={loadOverview} title="Refresh"><RefreshCw size={17} /></button></div></section><nav className="panel-tabs" aria-label="Server workspaces">{TABS.map(([key, label, Icon, permission]) => { const allowed = key === 'overview' || can(permission); return <button key={key} className={`${tab === key ? 'active' : ''} ${allowed ? '' : 'locked'}`} onClick={() => setTab(key)} title={allowed ? label : `Requires ${permission}`}><Icon size={16} />{label}{!allowed && <LockKeyhole className="tab-lock" size={12} />}</button> })}</nav>{renderTab()}</>}</main></div>
}
