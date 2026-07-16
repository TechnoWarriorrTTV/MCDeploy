import { Bot, FolderCog, Gauge, RadioTower, ShieldCheck, Workflow } from 'lucide-react'

const features = [
  [Gauge, 'Native control daemon', 'A compiled C++ service manages server processes, storage, metrics, backups, and lifecycle operations.'],
  [ShieldCheck, 'Webpanel access control', 'Assign emails per server, choose role presets, suspend access, and override individual permissions.'],
  [RadioTower, 'Live operational state', 'The webpanel presents current server state through permission-checked cloud API requests and reports temporary service interruptions clearly.'],
  [FolderCog, 'Server management', 'Work with console output, files, configuration, players, plugins, backups, and performance controls.'],
  [Workflow, 'Automation and maintenance', 'Create recovery rules, run scheduled operations, and enter maintenance mode with safer state handling.'],
  [Bot, 'AI server operator', 'Inspect logs and files, plan changes, request approval for destructive work, and retain an undo trail.']
]

export default function Features() {
  return (
    <section className="marketing-section" id="features">
      <div className="section-header"><span className="section-subtitle">The complete control plane</span><h2>Everything required to operate Minecraft infrastructure.</h2><p>One application for local ownership and one webpanel for delegated, permission-scoped access.</p></div>
      <div className="feature-grid">
        {features.map(([Icon, title, copy], index) => <article className="feature-card" key={title}><span className="feature-number">0{index + 1}</span><Icon /><h3>{title}</h3><p>{copy}</p></article>)}
      </div>
    </section>
  )
}