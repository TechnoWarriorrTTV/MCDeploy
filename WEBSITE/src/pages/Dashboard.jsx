import { useCallback, useEffect, useMemo, useState } from 'react'
import { ArrowRight, KeyRound, LoaderCircle, Search, Server, ShieldCheck } from 'lucide-react'
import { Link } from 'react-router-dom'
import { webpanelApi } from '../api/client.js'
import { useAuth } from '../auth/useAuth.js'
import ConnectionNotice from '../components/ConnectionNotice.jsx'
import PanelHeader from '../components/PanelHeader.jsx'

export default function Dashboard() {
  const { token, signOut } = useAuth()
  const [servers, setServers] = useState([])
  const [query, setQuery] = useState('')
  const [loading, setLoading] = useState(true)
  const [connected, setConnected] = useState(false)
  const [error, setError] = useState('')
  const [installationName, setInstallationName] = useState('My MCDeploy app')
  const [enrollment, setEnrollment] = useState(null)
  const [enrollmentBusy, setEnrollmentBusy] = useState(false)

  const load = useCallback(async () => {
    setLoading(true)
    try {
      const data = await webpanelApi.servers(token)
      setServers(data.servers || [])
      setConnected(true)
      setError('')
    } catch (requestError) {
      if (requestError.status === 401) return signOut()
      setConnected(!requestError.disconnected)
      setServers([])
      if (!requestError.disconnected) setError(requestError.message)
    } finally {
      setLoading(false)
    }
  }, [token, signOut])

  useEffect(() => {
    const initial = window.setTimeout(load, 0)
    const refresh = window.setInterval(load, 10000)
    return () => { window.clearTimeout(initial); window.clearInterval(refresh) }
  }, [load])

  const createEnrollmentCode = async event => {
    event.preventDefault()
    const name = installationName.trim()
    if (!name || enrollmentBusy) return
    setEnrollmentBusy(true)
    setError('')
    try {
      setEnrollment(await webpanelApi.createEnrollmentCode(token, name))
    } catch (requestError) {
      if (requestError.status === 401) return signOut()
      setError(requestError.message)
    } finally {
      setEnrollmentBusy(false)
    }
  }

  const filtered = useMemo(() => {
    const search = query.trim().toLowerCase()
    if (!search) return servers
    return servers.filter(server => `${server.name} ${server.software_type} ${server.version} ${server.role}`.toLowerCase().includes(search))
  }, [servers, query])

  return (
    <div className="panel-shell">
      <PanelHeader connected={connected} />
      <main className="panel-main">
        <div className="panel-title-row">
          <div><span className="eyebrow">Assigned access</span><h1>Your servers</h1><p>Only active server assignments for your email are shown.</p></div>
          <div className="search-box"><Search size={17} /><input value={query} onChange={event => setQuery(event.target.value)} placeholder="Search your servers" /></div>
        </div>

        {!connected && !loading && <ConnectionNotice retry={load} busy={loading} />}
        {error && <div className="form-error" role="alert">{error}</div>}

        <section className="workspace-card">
          <header className="workspace-head">
            <div><h2>Connect an MCDeploy installation</h2><p>Generate a single-use code for the native app. The code expires after ten minutes.</p></div>
          </header>
          <form className="command-form" onSubmit={createEnrollmentCode}>
            <KeyRound size={17} />
            <input value={installationName} maxLength={100} onChange={event => setInstallationName(event.target.value)} placeholder="Installation name" />
            <button className="button button-primary" disabled={!installationName.trim() || enrollmentBusy}>{enrollmentBusy ? 'Generating…' : 'Generate code'}</button>
          </form>
          {enrollment?.code && <div className="empty-state compact"><small>Single-use enrollment code</small><h2>{enrollment.code}</h2><p>Enter this code in the native MCDeploy cloud-agent setup. Do not share it.</p></div>}
        </section>

        {loading ? (
          <div className="loading-state"><LoaderCircle className="spin" /> Loading assigned servers…</div>
        ) : connected && filtered.length === 0 ? (
          <div className="empty-state"><ShieldCheck size={34} /><h2>No server access assigned</h2><p>The owner has not assigned this email to a server, or your access is suspended.</p></div>
        ) : connected && (
          <div className="server-grid">
            {filtered.map(server => (
              <Link to={`/panel/server/${server.uuid}`} className="server-card" key={server.uuid}>
                <div className="server-card-head">
                  <span className="server-avatar"><Server size={20} /></span>
                  <div><h2>{server.name}</h2><p>{server.software_type} {server.version}</p></div>
                  <span className={`state-pill ${server.status === 'Online' ? 'online' : 'offline'}`}><i />{server.status}</span>
                </div>
                <div className="server-card-details">
                  <span><small>Role</small><strong>{server.role}</strong></span>
                  <span><small>Port</small><strong>{server.port}</strong></span>
                  <span><small>Memory limit</small><strong>{server.ram_max} MB</strong></span>
                </div>
                <div className="server-card-foot"><span>{server.public_url || 'Private server'}</span><b>Open panel <ArrowRight size={15} /></b></div>
              </Link>
            ))}
          </div>
        )}
      </main>
    </div>
  )
}