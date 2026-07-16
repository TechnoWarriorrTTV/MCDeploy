import { ArrowRight, Bot, CheckCircle2, Cpu, Server, TerminalSquare } from 'lucide-react'
import { Link } from 'react-router-dom'

export default function Hero() {
  return (
    <section className="product-hero" id="top">
      <div className="hero-copy">
        <div className="hero-kicker"><span /> Native Minecraft operations</div>
        <h1>Run your servers.<br /><em>Not your panel.</em></h1>
        <p>MCDeploy combines a native C++ daemon, a permission-aware webpanel, automation, maintenance controls, and an AI operator in one focused control plane.</p>
        <div className="hero-actions left">
          <Link className="button button-primary" to="/login">Open webpanel <ArrowRight size={17} /></Link>
          <a className="button button-secondary" href="#installation">Build MCDeploy</a>
        </div>
        <div className="hero-trust"><span><CheckCircle2 /> Self-hosted</span><span><CheckCircle2 /> Granular access</span><span><CheckCircle2 /> Real-time control</span></div>
      </div>
      <div className="hero-product" aria-label="MCDeploy interface preview">
        <div className="product-window-bar"><i /><i /><i /><span>MCDeploy Control Plane</span></div>
        <div className="product-layout">
          <aside><b><Server size={16} /> Servers</b><span className="active"><i /> Survival</span><span><i /> Creative</span><span><i /> Proxy</span><small>Operations</small><span><Bot size={14} /> AI Copilot</span><span><TerminalSquare size={14} /> Audit logs</span></aside>
          <div className="product-content">
            <div className="preview-heading"><div><small>SERVER OVERVIEW</small><h3>Survival Network</h3></div><span className="preview-status"><i /> Permission scoped</span></div>
            <div className="preview-metrics"><article><Cpu /><small>HOST CPU</small><strong>Live telemetry</strong><span className="preview-bar"><i style={{ width: '68%' }} /></span></article><article><Server /><small>PROCESS</small><strong>Lifecycle control</strong><span className="preview-bar"><i style={{ width: '86%' }} /></span></article><article><Bot /><small>AUTOMATION</small><strong>Rules active</strong><span className="preview-bar"><i style={{ width: '54%' }} /></span></article></div>
            <div className="preview-console"><div><span>Console stream</span><b>Permission protected</b></div><code><i>[MCDeploy]</i> Waiting for authenticated server data...</code><code><i>[Webpanel]</i> Access is scoped to the signed-in email.</code><code className="cursor">_</code></div>
          </div>
        </div>
      </div>
    </section>
  )
}