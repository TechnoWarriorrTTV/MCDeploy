import { useCallback, useMemo, useState } from 'react'
import { webpanelApi } from '../api/client.js'
import { AuthContext } from './context.js'

const TOKEN_KEY = 'mcdeploy_webpanel_token'
const EMAIL_KEY = 'mcdeploy_webpanel_email'

export function AuthProvider({ children }) {
  const [token, setToken] = useState(() => localStorage.getItem(TOKEN_KEY) || '')
  const [email, setEmail] = useState(() => localStorage.getItem(EMAIL_KEY) || '')

  const acceptSession = useCallback(async login => {
    await webpanelApi.session(login.token)
    const normalizedEmail = login.email.trim().toLowerCase()
    localStorage.setItem(TOKEN_KEY, login.token)
    localStorage.setItem(EMAIL_KEY, normalizedEmail)
    setToken(login.token)
    setEmail(normalizedEmail)
  }, [])

  const signIn = useCallback(async (nextEmail, password) => {
    await acceptSession(await webpanelApi.login(nextEmail.trim(), password))
  }, [acceptSession])

  const register = useCallback((nextEmail, password, displayName) => (
    webpanelApi.register(nextEmail.trim(), password, displayName.trim())
  ), [])

  const verifyRegistration = useCallback(async (nextEmail, code) => {
    await acceptSession(await webpanelApi.verifyRegistration(nextEmail.trim(), code))
  }, [acceptSession])

  const resendVerification = useCallback((nextEmail) => (
    webpanelApi.resendVerification(nextEmail.trim())
  ), [])

  const completeOAuth = useCallback(async code => {
    await acceptSession(await webpanelApi.oauthExchange(code))
  }, [acceptSession])

  const signOut = useCallback(() => {
    localStorage.removeItem(TOKEN_KEY)
    localStorage.removeItem(EMAIL_KEY)
    setToken('')
    setEmail('')
  }, [])

  const value = useMemo(
    () => ({ token, email, signIn, register, verifyRegistration, resendVerification, completeOAuth, signOut }),
    [token, email, signIn, register, verifyRegistration, resendVerification, completeOAuth, signOut]
  )
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>
}