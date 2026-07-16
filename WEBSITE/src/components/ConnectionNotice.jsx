import { CircleAlert, RefreshCw } from 'lucide-react'

export const DISCONNECTED_MESSAGE = 'MCDeploy cloud service is temporarily unavailable. Please try again.'

export default function ConnectionNotice({ retry, busy = false }) {
  return (
    <div className="connection-notice" role="alert">
      <span className="connection-icon"><CircleAlert size={22} /></span>
      <div>
        <strong>Cloud service temporarily unavailable</strong>
        <p>{DISCONNECTED_MESSAGE}</p>
      </div>
      {retry && (
        <button className="button button-secondary" onClick={retry} disabled={busy}>
          <RefreshCw size={16} className={busy ? 'spin' : ''} /> Retry
        </button>
      )}
    </div>
  )
}