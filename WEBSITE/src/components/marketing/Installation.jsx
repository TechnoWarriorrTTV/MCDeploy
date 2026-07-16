import { useState } from 'react'
import { Check, Clipboard, TerminalSquare } from 'lucide-react'

const steps = [
  ['prerequisites', '1. Prerequisites', `Install CMake, Node.js, a C++20 compiler, and vcpkg dependencies declared in vcpkg.json.`],
  ['backend', '2. Compile C++', `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<your-vcpkg-toolchain>\ncmake --build build --config Release`],
  ['frontend', '3. Compile React', `cd frontend\nnpm install\nnpm run build`],
  ['website', '4. Compile Website', `cd WEBSITE\nnpm install\nnpm run build`],
  ['launch', '5. Run MCDeploy', `build\\Release\\mcdeploy.exe`]
]

export default function Installation() {
  const [active, setActive] = useState('prerequisites')
  const selected = steps.find(step => step[0] === active)
  const copy = () => navigator.clipboard?.writeText(selected[2])
  return (
    <section className="installation-section" id="installation">
      <div className="section-header"><span className="section-subtitle">Build and deploy</span><h2>Compile the native app and both React interfaces.</h2><p>The administrative interface builds to the root distribution; this public member website builds independently under WEBSITE/dist.</p></div>
      <div className="install-shell"><div className="install-tabs">{steps.map(([key, label]) => <button className={active === key ? 'active' : ''} key={key} onClick={() => setActive(key)}>{label}</button>)}</div><div className="install-terminal"><header><span><i /><i /><i /></span><b><TerminalSquare /> MCDeploy build workflow</b><button onClick={copy}><Clipboard /> Copy</button></header><pre>{selected[2]}</pre><footer><Check /> Run commands from the MCDeploy repository root unless the step changes directory.</footer></div></div>
    </section>
  )
}