import { Check, DatabaseBackup, FileCode2, HeartPulse, ServerCog, Users } from 'lucide-react'

const operations = [
  ['Lifecycle', 'Start, stop, restart, force-kill, import, and maintenance workflows.'],
  ['Configuration', 'RAM, ports, server properties, performance policy, and DNS state.'],
  ['Recovery', 'Backups, health scoring, audit history, and automated remediation rules.'],
  ['Delegation', 'Email assignments with per-server roles and granular action permissions.']
]

export default function Operations() {
  return (
    <section className="operations-section">
      <div className="operations-visual">
        <div className="ops-sidebar"><ServerCog /><span className="active">Overview</span><span>Console</span><span>Files</span><span>Players</span><span>Backups</span></div>
        <div className="ops-workspace"><div className="ops-title"><div><small>SERVER HEALTH</small><strong>Operational readiness</strong></div><HeartPulse /></div><div className="health-score-preview"><b>Health is calculated from live app data</b><span>No generated metrics or simulated server state.</span></div><div className="ops-list"><span><FileCode2 /> Config and files <Check /></span><span><Users /> Player operations <Check /></span><span><DatabaseBackup /> Recovery data <Check /></span></div></div>
      </div>
      <div className="operations-copy"><span className="section-subtitle">Designed for real operations</span><h2>Previewable changes. Clear ownership. Fewer surprises.</h2><p>The owner keeps the native app and administrative controls. Members use the public webpanel and see only the servers and actions assigned to their email.</p><div className="operations-points">{operations.map(([title, copy]) => <article key={title}><h3>{title}</h3><p>{copy}</p></article>)}</div></div>
    </section>
  )
}