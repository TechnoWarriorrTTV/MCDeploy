import { Bot, CheckCircle2, FileSearch, RotateCcw, ShieldAlert, TerminalSquare } from 'lucide-react'

const tools = [
  [FileSearch, 'Inspect', 'Read files, search logs, review configuration, and collect server context.'],
  [TerminalSquare, 'Operate', 'Run approved console actions and server lifecycle tasks through the app.'],
  [ShieldAlert, 'Confirm', 'Gate destructive or privileged actions behind explicit approval and permissions.'],
  [RotateCcw, 'Reverse', 'Track AI-caused changes and retain an undo path for supported file operations.']
]

export default function AiShowcase() {
  return (
    <section className="ai-showcase-section" id="ai-showcase">
      <div className="section-header"><span className="section-subtitle">AI Copilot</span><h2>A server operator, not a decorative chatbot.</h2><p>The agent works against the same files, logs, controls, and permission model as the rest of MCDeploy.</p></div>
      <div className="ai-showcase-grid"><div className="ai-tool-grid">{tools.map(([Icon, title, copy]) => <article key={title}><Icon /><div><h3>{title}</h3><p>{copy}</p></div></article>)}</div><div className="ai-chat-preview"><div className="ai-preview-head"><span><Bot /> MCDeploy Copilot</span><b>AGENT MODE</b></div><div className="ai-preview-message user">Why did the server stop, and can you recover it safely?</div><div className="ai-preview-step"><CheckCircle2 /> Read recent process and console logs</div><div className="ai-preview-step"><CheckCircle2 /> Inspect current process state and health</div><div className="ai-preview-step pending"><span /> Await approval before restart</div><div className="ai-preview-message bot">The app is reachable, the game process is stopped, and the last output indicates a clean shutdown. I can start it without modifying files.</div></div></div>
    </section>
  )
}