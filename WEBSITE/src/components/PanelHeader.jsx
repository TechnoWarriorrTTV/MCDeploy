import { Cloud, LogOut } from 'lucide-react'
import { Link } from 'react-router-dom'
import { useAuth } from '../auth/useAuth.js'
import Brand from './Brand.jsx'

export default function PanelHeader({ connected }) {
  const { email, signOut } = useAuth()
  return (
    <header className="panel-header">
      <Link to="/panel" className="header-brand"><Brand compact /></Link>
      <div className="header-connection">
        <span className={`status-dot ${connected ? 'online' : 'offline'}`} />
        <Cloud size={15} /> {connected ? 'Cloud service online' : 'Cloud service unavailable'}
      </div>
      <div className="header-account">
        <span className="account-email">{email}</span>
        <button className="icon-button" onClick={signOut} title="Sign out" aria-label="Sign out">
          <LogOut size={18} />
        </button>
      </div>
    </header>
  )
}