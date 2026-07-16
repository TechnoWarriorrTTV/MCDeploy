import React from 'react';
import { Link } from 'react-router-dom';
import {
  Bot, Server, Activity, Shield, Cpu, Puzzle, Users, Database,
  Terminal, Zap, ArrowRight, Github, Check, Play, Code
} from 'lucide-react';

// ============================================================================
// MCDeploy marketing landing page. Rendered at "/" — public-facing.
// Uses the same Tailwind + design tokens as the dashboard so the visual
// language stays consistent.
// ============================================================================

const Feature = ({ icon: Icon, title, children }) => (
  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-xl p-6 hover:border-mcdeploy-green/60 transition-colors">
    <div className="w-11 h-11 rounded-lg bg-mcdeploy-green/10 border border-mcdeploy-green/30 flex items-center justify-center mb-4">
      <Icon className="w-5 h-5 text-mcdeploy-green" />
    </div>
    <h3 className="text-white font-bold text-base mb-2">{title}</h3>
    <p className="text-mcdeploy-muted text-sm leading-relaxed">{children}</p>
  </div>
);

const CodeChip = ({ children }) => (
  <span className="inline-block bg-slate-950 border border-mcdeploy-border rounded px-2 py-0.5 font-mono text-xs text-mcdeploy-green">
    {children}
  </span>
);

export default function Landing() {
  return (
    <div className="min-h-screen bg-mcdeploy-bg text-white">
      {/* Top nav */}
      <header className="border-b border-mcdeploy-border/60 bg-mcdeploy-bg/80 backdrop-blur sticky top-0 z-40">
        <div className="max-w-7xl mx-auto px-6 py-3 flex items-center justify-between">
          <Link to="/" className="flex items-center gap-2">
            <div className="w-8 h-8 rounded-lg bg-mcdeploy-green/20 border border-mcdeploy-green/40 flex items-center justify-center">
              <Server className="w-4 h-4 text-mcdeploy-green" />
            </div>
            <span className="font-black text-lg tracking-tight">MCDeploy</span>
          </Link>
          <nav className="hidden md:flex items-center gap-6 text-sm text-mcdeploy-muted">
            <a href="#features" className="hover:text-white transition-colors">Features</a>
            <a href="#ai" className="hover:text-white transition-colors">AI Editor</a>
            <a href="#stack" className="hover:text-white transition-colors">Stack</a>
            <Link to="/sandbox" className="hover:text-white transition-colors">Live Demo</Link>
          </nav>
          <div className="flex items-center gap-2">
            <Link
              to="/sandbox"
              className="hidden sm:inline-flex items-center gap-1.5 text-xs font-semibold text-mcdeploy-muted hover:text-white px-3 py-2 rounded-lg transition-colors"
            >
              <Play className="w-3.5 h-3.5" /> Try Demo
            </Link>
            <Link
              to="/app"
              className="inline-flex items-center gap-1.5 text-xs font-bold bg-mcdeploy-green hover:bg-mcdeploy-green/85 text-black px-4 py-2 rounded-lg transition-colors"
            >
              Open Dashboard <ArrowRight className="w-3.5 h-3.5" />
            </Link>
          </div>
        </div>
      </header>

      {/* Hero */}
      <section className="max-w-7xl mx-auto px-6 pt-16 pb-24 md:pt-24 md:pb-32 text-center relative overflow-hidden">
        {/* Ambient background gradient */}
        <div className="absolute inset-0 -z-10 pointer-events-none">
          <div className="absolute top-0 left-1/2 -translate-x-1/2 w-[800px] h-[400px] bg-mcdeploy-green/10 blur-3xl rounded-full" />
        </div>

        <div className="inline-flex items-center gap-2 bg-mcdeploy-card border border-mcdeploy-border rounded-full px-3 py-1 text-xs text-mcdeploy-muted mb-6">
          <span className="w-1.5 h-1.5 rounded-full bg-mcdeploy-green animate-pulse" />
          Native C++ backend · Real-time console · AI-native
        </div>

        <h1 className="text-5xl md:text-7xl font-black tracking-tight leading-[0.95] mb-6">
          The Minecraft server panel that
          <br />
          <span className="text-mcdeploy-green">actually feels like software.</span>
        </h1>

        <p className="text-lg md:text-xl text-mcdeploy-muted max-w-2xl mx-auto mb-10 leading-relaxed">
          Deploy, monitor, and manage Minecraft servers with a native C++ backend, a real-time
          dashboard, and an AI assistant that reads your logs, edits your configs, and runs the
          commands you'd normally SSH in to type yourself.
        </p>

        <div className="flex flex-col sm:flex-row items-center justify-center gap-3">
          <Link
            to="/sandbox"
            className="inline-flex items-center gap-2 bg-mcdeploy-green hover:bg-mcdeploy-green/85 text-black text-sm font-bold px-6 py-3 rounded-lg transition-colors"
          >
            <Play className="w-4 h-4" /> Try the Live Sandbox
          </Link>
          <Link
            to="/app"
            className="inline-flex items-center gap-2 bg-mcdeploy-card border border-mcdeploy-border hover:border-mcdeploy-green/60 text-white text-sm font-bold px-6 py-3 rounded-lg transition-colors"
          >
            Open the Dashboard <ArrowRight className="w-4 h-4" />
          </Link>
        </div>

        {/* Screenshot mock */}
        <div className="mt-16 relative max-w-5xl mx-auto">
          <div className="absolute -inset-4 bg-gradient-to-b from-mcdeploy-green/20 to-transparent blur-2xl -z-10 rounded-3xl" />
          <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-xl shadow-2xl overflow-hidden">
            <div className="flex items-center gap-1.5 px-4 py-3 border-b border-mcdeploy-border/60 bg-black/30">
              <div className="w-3 h-3 rounded-full bg-red-500/70" />
              <div className="w-3 h-3 rounded-full bg-yellow-500/70" />
              <div className="w-3 h-3 rounded-full bg-mcdeploy-green/70" />
              <div className="flex-1 text-center text-xs text-mcdeploy-muted font-mono">
                mcdeploy.online / dashboard
              </div>
            </div>
            <div className="grid grid-cols-4 gap-4 p-6 bg-black/40">
              <div className="col-span-1 space-y-3">
                <div className="h-8 bg-mcdeploy-green/20 rounded" />
                <div className="h-6 bg-mcdeploy-border/50 rounded" />
                <div className="h-6 bg-mcdeploy-border/50 rounded" />
                <div className="h-6 bg-mcdeploy-border/50 rounded" />
                <div className="h-6 bg-mcdeploy-border/40 rounded" />
              </div>
              <div className="col-span-3 space-y-3">
                <div className="grid grid-cols-3 gap-3">
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded p-3">
                    <div className="text-xs text-mcdeploy-muted">CPU</div>
                    <div className="text-mcdeploy-green font-black text-xl">27%</div>
                  </div>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded p-3">
                    <div className="text-xs text-mcdeploy-muted">RAM</div>
                    <div className="text-mcdeploy-green font-black text-xl">3.2 GB</div>
                  </div>
                  <div className="bg-mcdeploy-card border border-mcdeploy-border rounded p-3">
                    <div className="text-xs text-mcdeploy-muted">TPS</div>
                    <div className="text-mcdeploy-green font-black text-xl">19.98</div>
                  </div>
                </div>
                <div className="bg-black/60 border border-mcdeploy-border rounded h-40 p-3 font-mono text-[10px] text-mcdeploy-muted overflow-hidden">
                  <div>[INFO] Server started on port 25565</div>
                  <div>[INFO] Loaded plugin EssentialsX v2.20.1</div>
                  <div>[INFO] Loaded plugin WorldEdit v7.3.0</div>
                  <div className="text-mcdeploy-green">[AI] Detected TPS drop, checking hopper chains…</div>
                  <div>[INFO] Steve joined the game</div>
                  <div>[INFO] Player count: 1/50</div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

      {/* Feature grid */}
      <section id="features" className="max-w-7xl mx-auto px-6 py-24 border-t border-mcdeploy-border/60">
        <div className="text-center max-w-2xl mx-auto mb-16">
          <div className="text-xs font-bold uppercase tracking-widest text-mcdeploy-green mb-3">Everything you need</div>
          <h2 className="text-4xl md:text-5xl font-black tracking-tight mb-4">One panel. Zero SSH sessions.</h2>
          <p className="text-mcdeploy-muted">
            MCDeploy replaces the four tabs you'd normally have open to run a Minecraft server: file editor,
            SSH terminal, plugin marketplace, and monitoring dashboard.
          </p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          <Feature icon={Bot} title="AI Server Assistant">
            Tool-calling agent with 40+ tools. Reads server.properties, greps logs, installs plugins from
            Modrinth, edits configs, restarts your server. Not a chatbot — a real assistant.
          </Feature>
          <Feature icon={Server} title="One-Click Deployment">
            Spin up Paper, Purpur, Spigot, Vanilla, or Forge servers by picking a version and clicking a button.
            MCDeploy fetches the correct jar, wires the JVM args, and starts the process.
          </Feature>
          <Feature icon={Activity} title="Live Metrics">
            CPU, RAM, disk, TPS, MSPT, entity counts, chunk counts — all streamed over WebSocket. No
            polling, no page refreshes.
          </Feature>
          <Feature icon={Terminal} title="Real-Time Console">
            Attached stdin/stdout to the Java process. Type commands, watch chat messages, see plugin
            output live. Every line is timestamped and colour-coded by severity.
          </Feature>
          <Feature icon={Puzzle} title="Plugin & Mod Manager">
            Search Modrinth from inside the panel. One-click install, version pinning, uninstall. Works for
            Bukkit plugins and Forge/Fabric mods alike.
          </Feature>
          <Feature icon={Users} title="Player Management">
            View every player's inventory, ender chest, coordinates, and advancements. Edit slots directly,
            wipe inventories, or fully reset a player.
          </Feature>
          <Feature icon={Database} title="Automatic Backups">
            Scheduled or on-demand ZIP backups of the entire server directory. Restore with one click.
          </Feature>
          <Feature icon={Shield} title="Cloudflare DNS Integration">
            Assign <CodeChip>your-server.mcdeploy.online</CodeChip> subdomains automatically. A + SRV
            records provisioned via the Cloudflare API.
          </Feature>
          <Feature icon={Cpu} title="Native C++ Backend">
            Not a Node.js panel wrapping other panels. MCDeploy is written in modern C++20 on Drogon +
            SQLite, so it uses 30 MB of RAM instead of 300.
          </Feature>
        </div>
      </section>

      {/* AI showcase */}
      <section id="ai" className="max-w-7xl mx-auto px-6 py-24 border-t border-mcdeploy-border/60">
        <div className="grid md:grid-cols-2 gap-12 items-center">
          <div>
            <div className="text-xs font-bold uppercase tracking-widest text-mcdeploy-green mb-3">
              AI-native since day one
            </div>
            <h2 className="text-4xl md:text-5xl font-black tracking-tight mb-6">
              An assistant that actually does the work.
            </h2>
            <p className="text-mcdeploy-muted mb-6 leading-relaxed">
              The AI editor isn't a wrapper around GPT. It's a full tool-calling agent with direct
              access to your server files, running Java process, plugin directory, and player database.
              Say "why did the server crash last night?" and it'll grep the logs, correlate errors, and
              show you the diff to fix it.
            </p>
            <ul className="space-y-3 text-sm">
              <li className="flex items-start gap-3">
                <Check className="w-5 h-5 text-mcdeploy-green flex-shrink-0 mt-0.5" />
                <span><b className="text-white">Reads and edits config files</b> with a diff preview before writing</span>
              </li>
              <li className="flex items-start gap-3">
                <Check className="w-5 h-5 text-mcdeploy-green flex-shrink-0 mt-0.5" />
                <span><b className="text-white">Undo stack</b> for every AI-caused change, backed by SQLite</span>
              </li>
              <li className="flex items-start gap-3">
                <Check className="w-5 h-5 text-mcdeploy-green flex-shrink-0 mt-0.5" />
                <span><b className="text-white">Confirmation modals</b> for destructive actions — the AI never surprises you</span>
              </li>
              <li className="flex items-start gap-3">
                <Check className="w-5 h-5 text-mcdeploy-green flex-shrink-0 mt-0.5" />
                <span><b className="text-white">Works with Gemini, Groq, DeepSeek, OpenAI, and Mistral</b> — any OpenAI-compat API</span>
              </li>
              <li className="flex items-start gap-3">
                <Check className="w-5 h-5 text-mcdeploy-green flex-shrink-0 mt-0.5" />
                <span><b className="text-white">Slash commands</b> like <CodeChip>/logs</CodeChip>, <CodeChip>/plugins</CodeChip>, <CodeChip>/restart</CodeChip></span>
              </li>
            </ul>
          </div>

          <div className="bg-mcdeploy-card border border-mcdeploy-border rounded-xl overflow-hidden shadow-2xl">
            <div className="border-b border-mcdeploy-border/60 px-4 py-3 flex items-center gap-2 bg-black/30">
              <Bot className="w-4 h-4 text-mcdeploy-green" />
              <span className="text-sm font-bold">AI Server Assistant</span>
              <span className="ml-auto text-[10px] text-mcdeploy-muted font-mono">agent mode</span>
            </div>
            <div className="p-5 space-y-4 text-sm">
              <div className="text-right">
                <span className="inline-block bg-blue-600 text-white rounded-lg px-3 py-2">
                  can you make the RAM 2G to 4G?
                </span>
              </div>
              <div>
                <div className="inline-block bg-black/40 border border-mcdeploy-border rounded-lg px-4 py-3 max-w-full">
                  <div className="font-mono text-xs text-mcdeploy-green mb-1.5">→ update_server_config({"{"} ram_min_mb: 2048, ram_max_mb: 4096 {"}"})</div>
                  <div className="text-mcdeploy-muted text-xs mb-2 font-mono">✓ ram_min = 2048 MB, ram_max = 4096 MB. JVM args rewritten.</div>
                  <div className="text-white">
                    Updated. Restart the server to apply the new RAM allocation.
                  </div>
                  <div className="mt-3 flex gap-1.5 flex-wrap">
                    <span className="text-[11px] bg-mcdeploy-bg border border-mcdeploy-border px-2 py-1 rounded">Restart the server</span>
                    <span className="text-[11px] bg-mcdeploy-bg border border-mcdeploy-border px-2 py-1 rounded">Check TPS</span>
                    <span className="text-[11px] bg-mcdeploy-bg border border-mcdeploy-border px-2 py-1 rounded">Show me the diff</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

      {/* Stack */}
      <section id="stack" className="max-w-7xl mx-auto px-6 py-24 border-t border-mcdeploy-border/60">
        <div className="text-center max-w-2xl mx-auto mb-16">
          <div className="text-xs font-bold uppercase tracking-widest text-mcdeploy-green mb-3">Under the hood</div>
          <h2 className="text-4xl md:text-5xl font-black tracking-tight mb-4">Boring stack, fast panel.</h2>
          <p className="text-mcdeploy-muted">
            MCDeploy is built on load-bearing technology. No experimental frameworks, no vendor lock-in.
          </p>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          {[
            { name: 'C++20', hint: 'Native performance' },
            { name: 'Drogon', hint: 'Async HTTP + WebSocket' },
            { name: 'SQLite', hint: 'Zero-config persistence' },
            { name: 'React 19', hint: 'Modern UI, no jank' },
            { name: 'Tailwind', hint: 'Design tokens' },
            { name: 'Gemini API', hint: 'Free tier, tool calling' },
            { name: 'Modrinth', hint: 'Plugin discovery' },
            { name: 'Cloudflare', hint: 'DNS + tunneling' },
          ].map((t) => (
            <div key={t.name} className="bg-mcdeploy-card border border-mcdeploy-border rounded-lg p-4 text-center">
              <div className="text-white font-bold text-sm">{t.name}</div>
              <div className="text-mcdeploy-muted text-xs mt-1">{t.hint}</div>
            </div>
          ))}
        </div>
      </section>

      {/* Final CTA */}
      <section className="max-w-3xl mx-auto px-6 py-24 text-center">
        <h2 className="text-4xl md:text-5xl font-black tracking-tight mb-4">Ready to run your server?</h2>
        <p className="text-mcdeploy-muted mb-8 text-lg">
          Try the sandbox with fake data first, or jump straight into the dashboard.
        </p>
        <div className="flex flex-col sm:flex-row gap-3 justify-center">
          <Link
            to="/sandbox"
            className="inline-flex items-center gap-2 bg-mcdeploy-green hover:bg-mcdeploy-green/85 text-black text-sm font-bold px-6 py-3 rounded-lg transition-colors"
          >
            <Play className="w-4 h-4" /> Try the Live Sandbox
          </Link>
          <Link
            to="/app"
            className="inline-flex items-center gap-2 bg-mcdeploy-card border border-mcdeploy-border hover:border-mcdeploy-green/60 text-white text-sm font-bold px-6 py-3 rounded-lg transition-colors"
          >
            <Zap className="w-4 h-4" /> Open Dashboard
          </Link>
        </div>
      </section>

      {/* Footer */}
      <footer className="border-t border-mcdeploy-border/60 py-8">
        <div className="max-w-7xl mx-auto px-6 flex flex-col md:flex-row items-center justify-between gap-4 text-xs text-mcdeploy-muted">
          <div className="flex items-center gap-2">
            <Server className="w-4 h-4 text-mcdeploy-green" />
            <span className="font-bold text-white">MCDeploy</span>
            <span>· mcdeploy.online</span>
          </div>
          <div className="flex items-center gap-4">
            <a href="#features" className="hover:text-white transition-colors">Features</a>
            <Link to="/sandbox" className="hover:text-white transition-colors">Demo</Link>
            <Link to="/app" className="hover:text-white transition-colors">Dashboard</Link>
            <a
              href="https://github.com"
              target="_blank"
              rel="noreferrer"
              className="hover:text-white transition-colors inline-flex items-center gap-1"
            >
              <Github className="w-3.5 h-3.5" /> GitHub
            </a>
          </div>
        </div>
      </footer>
    </div>
  );
}
