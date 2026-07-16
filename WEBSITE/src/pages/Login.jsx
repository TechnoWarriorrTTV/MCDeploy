import { useEffect, useRef, useState } from 'react'
import { ArrowLeft, CheckCircle2, Code2, Globe, LoaderCircle, LockKeyhole, Mail, MessageCircle, RotateCw, UserPlus } from 'lucide-react'
import { Link, Navigate, useNavigate, useSearchParams } from 'react-router-dom'
import { useAuth } from '../auth/useAuth.js'
import { webpanelApi } from '../api/client.js'
import Brand from '../components/Brand.jsx'
import { DISCONNECTED_MESSAGE } from '../components/ConnectionNotice.jsx'

const PROVIDERS = [
  { id: 'google', label: 'Google', Icon: Globe },
  { id: 'github', label: 'GitHub', Icon: Code2 },
  { id: 'discord', label: 'Discord', Icon: MessageCircle }
]

export default function Login() {
  const { token, signIn, register, verifyRegistration, resendVerification, completeOAuth } = useAuth()
  const navigate = useNavigate()
  const [searchParams] = useSearchParams()
  const oauthStarted = useRef(false)
  const [mode, setMode] = useState('login')
  const [email, setEmail] = useState('')
  const [displayName, setDisplayName] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [verificationCode, setVerificationCode] = useState('')
  const [pendingEmail, setPendingEmail] = useState('')
  const [resendSeconds, setResendSeconds] = useState(0)
  const [emailVerificationEnabled, setEmailVerificationEnabled] = useState(false)
  const [busy, setBusy] = useState(false)
  const [connected, setConnected] = useState(null)
  const [enabledProviders, setEnabledProviders] = useState([])
  const [error, setError] = useState(searchParams.get('oauth_error') || '')

  useEffect(() => {
    let active = true
    webpanelApi.status()
      .then(data => {
        if (!active) return
        setConnected(true)
        setEnabledProviders(data.oauth_providers || [])
        setEmailVerificationEnabled(Boolean(data.email_verification))
      })
      .catch(() => active && setConnected(false))
    return () => { active = false }
  }, [])

  useEffect(() => {
    const code = searchParams.get('oauth_code')
    if (!code || oauthStarted.current) return
    oauthStarted.current = true
    setBusy(true)
    setError('')
    completeOAuth(code)
      .then(() => navigate('/panel', { replace: true }))
      .catch(requestError => setError(requestError.disconnected ? DISCONNECTED_MESSAGE : requestError.message))
      .finally(() => setBusy(false))
  }, [searchParams, completeOAuth, navigate])

  useEffect(() => {
    if (resendSeconds <= 0) return undefined
    const timer = window.setInterval(() => setResendSeconds(value => Math.max(0, value - 1)), 1000)
    return () => window.clearInterval(timer)
  }, [resendSeconds])

  if (token) return <Navigate to="/panel" replace />

  const submit = async event => {
    event.preventDefault()
    if (mode === 'register' && password !== confirmPassword) {
      setError('Passwords do not match.')
      return
    }
    setBusy(true)
    setError('')
    try {
      if (mode === 'register') {
        const pending = await register(email, password, displayName)
        setPendingEmail(pending.email || email.trim().toLowerCase())
        setVerificationCode('')
        setResendSeconds(60)
        setMode('verify')
      } else if (mode === 'verify') {
        await verifyRegistration(pendingEmail, verificationCode)
        navigate('/panel', { replace: true })
      } else {
        await signIn(email, password)
        navigate('/panel', { replace: true })
      }
    } catch (requestError) {
      setConnected(!requestError.disconnected)
      setError(requestError.disconnected ? DISCONNECTED_MESSAGE : requestError.message)
    } finally {
      setBusy(false)
    }
  }

  const resendCode = async () => {
    if (resendSeconds > 0 || busy) return
    setBusy(true)
    setError('')
    try {
      await resendVerification(pendingEmail)
      setResendSeconds(60)
    } catch (requestError) {
      setError(requestError.disconnected ? DISCONNECTED_MESSAGE : requestError.message)
    } finally {
      setBusy(false)
    }
  }

  const chooseMode = nextMode => {
    setMode(nextMode)
    setError('')
    setPassword('')
    setConfirmPassword('')
  }

  return (
    <main className="auth-page">
      <Link className="back-link" to="/"><ArrowLeft size={16} /> Back to MCDeploy</Link>
      <section className="auth-card auth-card-wide">
        <Brand />

        {mode !== 'verify' && <div className="auth-mode-tabs" role="tablist" aria-label="Account access">
          <button type="button" className={mode === 'login' ? 'active' : ''} onClick={() => chooseMode('login')}>Sign in</button>
          <button type="button" className={mode === 'register' ? 'active' : ''} onClick={() => chooseMode('register')}>Create account</button>
        </div>}

        <div className="auth-heading">
          <h1>{mode === 'verify' ? 'Check your email' : mode === 'register' ? 'Create your account' : 'Welcome back'}</h1>
          <p>{mode === 'verify'
            ? `Enter the six-digit code sent to ${pendingEmail}. It expires in 10 minutes.`
            : mode === 'register'
              ? 'Choose your credentials, then verify your email before the account is created.'
              : 'Sign in with your own password or a verified social account.'}</p>
        </div>

        {mode !== 'verify' && <div className="oauth-grid">
          {PROVIDERS.map(({ id, label, Icon }) => {
            const enabled = enabledProviders.includes(id)
            return (
              <button
                key={id}
                type="button"
                className="oauth-button"
                disabled={busy || !enabled}
                title={enabled ? `Continue with ${label}` : `${label} sign-in is not configured yet`}
                onClick={() => window.location.assign(webpanelApi.oauthStartUrl(id))}
              >
                <Icon size={17} /> {label}
              </button>
            )
          })}
        </div>}
        {mode !== 'verify' && enabledProviders.length === 0 && connected && <p className="oauth-note">Social sign-in is ready but needs provider credentials configured by the MCDeploy owner.</p>}

        {mode !== 'verify' && <div className="auth-divider"><span>or use email</span></div>}
        {mode === 'register' && connected === false && <div className="form-error">The MCDeploy account service is temporarily unavailable. Account creation will resume when the website API is connected.</div>}
        {mode === 'register' && !emailVerificationEnabled && connected && <div className="form-error">Manual signup is unavailable until email delivery is configured. You can still use an enabled social provider.</div>}
        {error && <div className="form-error" role="alert">{error}</div>}
        <form onSubmit={submit} className="auth-form">
          {mode === 'verify' ? (
            <>
              <label><span>Verification code</span><div className="input-wrap verification-wrap"><CheckCircle2 size={17} /><input inputMode="numeric" autoComplete="one-time-code" maxLength={6} pattern="[0-9]{6}" value={verificationCode} onChange={event => setVerificationCode(event.target.value.replace(/\D/g, '').slice(0, 6))} placeholder="000000" required /></div></label>
              <button className="button button-primary auth-submit" disabled={busy || verificationCode.length !== 6}>{busy ? <LoaderCircle size={17} className="spin" /> : <CheckCircle2 size={17} />}{busy ? 'Verifying…' : 'Verify and create account'}</button>
              <div className="verification-actions">
                <button type="button" className="button button-secondary" onClick={resendCode} disabled={busy || resendSeconds > 0}><RotateCw size={15} />{resendSeconds > 0 ? `Resend in ${resendSeconds}s` : 'Resend code'}</button>
                <button type="button" className="button button-ghost" onClick={() => chooseMode('register')} disabled={busy}>Use another email</button>
              </div>
            </>
          ) : (
            <>
          {mode === 'register' && (
            <label><span>Display name <small>optional</small></span><div className="input-wrap"><UserPlus size={17} /><input autoComplete="name" value={displayName} onChange={event => setDisplayName(event.target.value)} /></div></label>
          )}
          <label><span>Email address</span><div className="input-wrap"><Mail size={17} /><input type="email" autoComplete="email" value={email} onChange={event => setEmail(event.target.value)} required /></div></label>
          <label><span>Password</span><div className="input-wrap"><LockKeyhole size={17} /><input type="password" minLength={mode === 'register' ? 10 : undefined} maxLength={128} autoComplete={mode === 'register' ? 'new-password' : 'current-password'} value={password} onChange={event => setPassword(event.target.value)} required /></div></label>
          {mode === 'register' && (
            <label><span>Confirm password</span><div className="input-wrap"><LockKeyhole size={17} /><input type="password" minLength={10} maxLength={128} autoComplete="new-password" value={confirmPassword} onChange={event => setConfirmPassword(event.target.value)} required /></div></label>
          )}
          <button className="button button-primary auth-submit" disabled={busy || (mode === 'register' && !emailVerificationEnabled)}>
            {busy ? <LoaderCircle size={17} className="spin" /> : mode === 'register' ? <UserPlus size={17} /> : <LockKeyhole size={17} />}
            {busy ? 'Please wait…' : mode === 'register' ? 'Email verification code' : 'Sign in securely'}
          </button>
            </>
          )}
        </form>
        <p className="auth-help">Creating an account does not grant server access. Access is assigned separately by the server owner.</p>
      </section>
    </main>
  )
}