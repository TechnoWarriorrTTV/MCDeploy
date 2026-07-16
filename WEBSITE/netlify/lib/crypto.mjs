const encoder = new TextEncoder()
const PBKDF2_ITERATIONS = 210000

function bytesToBase64(bytes) {
  return Buffer.from(bytes).toString('base64')
}

export function randomBytes(length = 32) {
  const value = new Uint8Array(length)
  crypto.getRandomValues(value)
  return value
}

export function randomToken(length = 32) {
  return Buffer.from(randomBytes(length)).toString('base64url')
}

export function randomSixDigitCode() {
  const values = new Uint32Array(1)
  const limit = 0x100000000 - (0x100000000 % 1000000)
  do { crypto.getRandomValues(values) } while (values[0] >= limit)
  return String(values[0] % 1000000).padStart(6, '0')
}

export async function sha256(value) {
  const digest = await crypto.subtle.digest('SHA-256', encoder.encode(value))
  return Buffer.from(digest).toString('base64url')
}

export async function hashWithSecret(value, environmentName) {
  const secret = process.env[environmentName]
  if (!secret || secret.length < 32) throw new Error('Required server secret is unavailable.')
  return sha256(`${value}:${secret}`)
}

export function constantTimeEqual(left, right) {
  const a = encoder.encode(String(left))
  const b = encoder.encode(String(right))
  let mismatch = a.length ^ b.length
  const length = Math.max(a.length, b.length)
  for (let index = 0; index < length; index += 1) mismatch |= (a[index % a.length] || 0) ^ (b[index % b.length] || 0)
  return mismatch === 0
}

export async function hashPassword(password) {
  const salt = randomBytes(16)
  const key = await crypto.subtle.importKey('raw', encoder.encode(password), 'PBKDF2', false, ['deriveBits'])
  const derived = await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations: PBKDF2_ITERATIONS }, key, 256)
  return `pbkdf2-sha256$${PBKDF2_ITERATIONS}$${bytesToBase64(salt)}$${bytesToBase64(new Uint8Array(derived))}`
}

export async function verifyPassword(password, stored) {
  const [scheme, iterationText, saltText, expected] = String(stored || '').split('$')
  const iterations = Number(iterationText)
  if (scheme !== 'pbkdf2-sha256' || !Number.isInteger(iterations) || iterations < PBKDF2_ITERATIONS || !saltText || !expected) return false
  const salt = Buffer.from(saltText, 'base64')
  const key = await crypto.subtle.importKey('raw', encoder.encode(password), 'PBKDF2', false, ['deriveBits'])
  const derived = await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, key, 256)
  return constantTimeEqual(bytesToBase64(new Uint8Array(derived)), expected)
}
