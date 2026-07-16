import { Link } from 'react-router-dom'
import Brand from '../Brand.jsx'

export default function Footer() {
  return (
    <><section className="marketing-cta"><div><span className="section-subtitle">Your control plane is ready</span><h2>Configure access. Assign servers. Operate from anywhere.</h2><p>Owners manage membership in the native dashboard; members create verified accounts and receive only the server permissions assigned to them.</p></div><Link className="button button-primary" to="/login">Open webpanel</Link></section><footer className="full-footer"><div className="footer-brand"><Brand /><p>A native Minecraft operations panel with secure delegated web access.</p></div><div><strong>Product</strong><a href="#features">Features</a><Link to="/login">Webpanel</Link><a href="#ai-showcase">AI Copilot</a></div><div><strong>Build</strong><a href="#installation">Installation</a><Link to="/login">Member login</Link></div><div><strong>Runtime</strong><span>Native C++ daemon</span><span>React webpanel</span><span>SQLite persistence</span></div><small>© 2026 MCDeploy. Built for self-hosted Minecraft operations.</small></footer></>
  )
}