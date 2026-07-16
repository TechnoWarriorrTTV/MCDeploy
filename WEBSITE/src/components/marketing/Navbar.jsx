import { useState } from 'react'
import { Link } from 'react-router-dom'
import { Menu, X } from 'lucide-react'
import Brand from '../Brand.jsx'

const links = [
  ['Features', '#features'],
  ['AI Copilot', '#ai-showcase'],
  ['Installation', '#installation']
]

export default function Navbar() {
  const [open, setOpen] = useState(false)
  return (
    <header className="navbar">
      <a href="#top" className="nav-brand"><Brand /></a>
      <nav className={open ? 'nav-links open' : 'nav-links'} aria-label="Main navigation">
        {links.map(([label, href]) => <a key={href} href={href} onClick={() => setOpen(false)}>{label}</a>)}
        <Link className="button button-primary nav-login-mobile" to="/login" onClick={() => setOpen(false)}>Log in</Link>
      </nav>
      <div className="nav-actions"><Link className="button button-secondary" to="/login">Log in</Link></div>
      <button className="nav-toggle" onClick={() => setOpen(value => !value)} aria-expanded={open} aria-label="Toggle navigation">{open ? <X /> : <Menu />}</button>
    </header>
  )
}