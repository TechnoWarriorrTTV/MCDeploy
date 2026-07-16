import { HttpError } from './http.mjs'

export async function sendVerificationEmail(email, code) {
  const apiKey = process.env.RESEND_API_KEY
  const from = process.env.RESEND_FROM
  if (!apiKey || !from) throw new Error('Email delivery is unavailable.')
  const response = await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: { Authorization: `Bearer ${apiKey}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({
      from,
      to: [email],
      subject: 'Verify your MCDeploy account',
      text: `Your MCDeploy verification code is ${code}. It expires in 10 minutes. If you did not request this, ignore this email.`,
      html: `<p>Your MCDeploy verification code is:</p><p style="font-size:24px;font-weight:700;letter-spacing:4px">${code}</p><p>It expires in 10 minutes. If you did not request this, ignore this email.</p>`
    })
  })
  if (!response.ok) throw new HttpError(502, 'Verification email could not be delivered.')
}
