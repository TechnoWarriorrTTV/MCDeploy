# MCDeploy Webpanel

This directory contains the Vite/React webpanel and its Netlify backend. Production uses same-origin `/api` requests; keep `VITE_MCDEPLOY_API_URL` empty for production.

## Deploy from a private Git repository

Deploy the repository through Netlify's Git integration. Do not drag and drop `dist`: a static-only upload omits Functions and causes `/api/webpanel/*` to return 404.

1. Push this project to a **private** GitHub or GitLab repository. Do not commit `.env` files, keys, databases, server folders, backups, or native configuration.
2. In Netlify, choose **Add new project → Import an existing project** and select the repository.
3. Use these build settings:

   | Setting | Value |
   |---|---|
   | Base directory | `WEBSITE` |
   | Build command | `npm ci && npm run build` |
   | Publish directory | `dist` |

   `dist` is relative to the `WEBSITE` base directory. The checked-in `netlify.toml` also declares `netlify/functions` as the Functions directory.
4. Provision a Netlify Database for the site. Apply the tracked SQL files in `WEBSITE/migrations/` using the database workflow shown by Netlify; do not expose its connection string to React.
5. Add the server-side variables listed in `.env.example` in **Site configuration → Environment variables**, scoped to Functions. Generate every pepper/secret independently with a cryptographically secure generator.
6. Deploy and confirm the deploy log includes the Vite build and Function bundling. After database or environment changes, trigger a new deploy.

Verify the deployed backend rather than only the page:

```powershell
curl.exe -i https://your-domain.example/api/webpanel/status
```

It must return HTTP 200 JSON with `"status":"success"`. Also confirm the required account, webpanel, and agent handlers appear in Netlify's Functions view. A file present in `netlify/functions` is not proof that its handler is implemented or deployed.

## Local development and build

```powershell
cd WEBSITE
npm ci
npm run dev
npm run build
```

The production bundle is written to `WEBSITE/dist`. Hash routing allows client routes to work on static hosting. `VITE_MCDEPLOY_DEV_DAEMON` is development-only and controls Vite's local `/api` proxy target; it is not a production backend setting.

## Functions, database, and environment

- `netlify/functions/` contains HTTP entry points for browser account/webpanel traffic and outbound native-agent traffic.
- `netlify/lib/` contains shared authentication, database, HTTP, crypto, email, permission, and rate-limit helpers.
- `migrations/` is the authoritative tracked schema. Apply migrations before relying on the corresponding Functions.
- Netlify Database stores cloud identities, sessions, installation records, synchronized server metadata, memberships, queued commands, and command results.
- Secrets belong only in Netlify's server-side environment. Any `VITE_` variable is embedded in the browser bundle and must never contain a secret.
- Leave production `VITE_MCDEPLOY_API_URL` empty so requests stay on the deployed site's origin.

See `.env.example` for sanitized names and placeholders. Netlify-managed database connectivity should remain server-side; do not copy it into a `VITE_` variable.

## Cloud/native responsibility split

The cloud and native app have different responsibilities:

**Netlify can:** serve the React site, authenticate web users through implemented Functions, enforce cloud permissions, read/write the managed database, synchronize metadata supplied by an agent, queue idempotent commands, and return results uploaded by that agent.

**Netlify cannot:** start a Minecraft process, inspect or edit an owner's local files, create a local backup, run native controllers or shell commands, or obtain live machine state by itself. Functions enqueue allowed work; they never execute native server operations.

The installed MCDeploy application remains required. A native cloud agent must be implemented, enrolled, and running on each owner machine to authenticate outbound, send heartbeats and inventory, poll/claim commands, execute only an audited local allowlist, and upload sanitized results. Until that agent path is complete and deployed, live server requests will remain pending and then fail or expire; the website must not claim otherwise.

## Features that are not created by configuration alone

- **OAuth:** Client IDs and secrets do not create OAuth support. Provider-specific start, callback, state/PKCE validation, and identity-linking handlers must be implemented and deployed before a provider is advertised. The current status response advertises no OAuth providers.
- **DNS/subdomains:** A database reservation is not DNS publication. DNS requires a separately implemented, audited server-side integration with narrowly scoped credentials. Never place zone credentials in React or distributed native binaries.
- **Native execution:** A successful Function deploy does not provide native execution. Only the authenticated installed agent may run approved operations, after rechecking local policy.

## Operational checks

Before enabling users, verify that migrations match the deployed Functions, required secrets are present, registration/session flows return JSON rather than HTML, cross-installation authorization is denied, duplicate idempotency keys do not queue duplicate work, offline commands expire safely, and no Function response exposes credentials, database errors, stack traces, or local filesystem paths.