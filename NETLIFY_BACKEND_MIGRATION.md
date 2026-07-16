# MCDeploy Netlify Backend — Mandatory Owner Steps

The repository-side Netlify account backend is implemented. Complete only the steps below to put it online. Account creation can work after Steps 1–5; live Minecraft controls additionally require Step 6.

## 1. Put this workspace in a private Git repository

This workspace is a Git repository. To set it up on GitHub/GitLab:

```powershell
git init
git branch -M main
# Note: .gitignore and WEBSITE/ are ignored locally so they won't be pushed.
git add . 
git commit -m "Add MCDeploy source and configs"
git remote add origin <YOUR_PRIVATE_REPOSITORY_URL>
git push -u origin main
```

Review staged files before committing. Never add `config.json`, `.env` files, databases, backups, server folders, build output, or credentials.

## 2. Connect the repository to Netlify

Import the private repository in Netlify and use:

- **Base directory:** `WEBSITE`
- **Build command:** `npm ci && npm run build`
- **Publish directory:** `dist`

`WEBSITE/netlify.toml` already configures Node and `netlify/functions`. Do not use drag-and-drop deployment because it omits Functions.

## 3. Create and migrate Netlify Database

Create a Netlify Database for the site. Apply `WEBSITE/migrations/0001_initial.sql` using Netlify's Database migration workflow. Do not put its connection string in React or any `VITE_*` variable.

## 4. Add server-only environment variables

In Netlify **Site configuration → Environment variables**, add these variables for Functions:

### General & Authentication
- `RESEND_API_KEY` (Resend Email API Key)
- `RESEND_FROM` (e.g., `MCDeploy <info@mcdeploy.online>`)
- `SESSION_PEPPER`, `VERIFICATION_PEPPER`, `RATE_LIMIT_PEPPER`
- `AGENT_ENROLLMENT_SECRET`
- `PUBLIC_SITE_URL` (`https://mcdeploy.online`)

### Cloudflare DNS Proxy
- `CLOUDFLARE_API_TOKEN` (API Token with Zone.DNS edit permissions for your domain)
- `CLOUDFLARE_ZONE_ID` (The API Zone ID of your Cloudflare domain)
- `DNS_PROXY_SECRET` (A random secret token used by C++ clients to authorize DNS registrations)

Generate each pepper/secret/token independently:

```powershell
$bytes = New-Object byte[] 48
[System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
[Convert]::ToBase64String($bytes)
```

Never commit those generated values. Confirm the Resend sender/domain is verified, then trigger a new Netlify deploy.

## 5. Verify production

Run `curl.exe -i https://mcdeploy.online/api/webpanel/status`; it must return HTTP 200 JSON with `"api_version":4`. Create an account on the website, receive the six-digit email, verify it once, sign in, refresh the panel, and confirm the session remains valid. If the route returns Netlify HTML or 404, check that the deploy lists the Functions and that the Git deployment used `WEBSITE` as its base.

## 6. Required before enabling live server controls

The Netlify Functions now securely queue and route server commands, but the current native executable does **not** yet contain the authenticated outbound `CloudAgent` that executes them. Keep live controls private until a native build implements secure Windows credential storage, enrollment, heartbeat/inventory sync, command polling, a local action allowlist, and sanitized result upload. Netlify configuration cannot replace this local agent. The website dashboard can already generate a ten-minute enrollment code for that future native build.
