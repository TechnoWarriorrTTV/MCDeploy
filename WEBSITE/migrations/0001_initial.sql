CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE users (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  email text NOT NULL UNIQUE CHECK (email = lower(email)),
  display_name text NOT NULL DEFAULT '' CHECK (char_length(display_name) <= 80),
  password_hash text,
  status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'suspended')),
  created_at timestamptz NOT NULL DEFAULT now(),
  last_seen_at timestamptz
);

CREATE TABLE pending_registrations (
  email text PRIMARY KEY CHECK (email = lower(email)),
  display_name text NOT NULL DEFAULT '' CHECK (char_length(display_name) <= 80),
  password_hash text NOT NULL,
  verification_hash text NOT NULL,
  expires_at timestamptz NOT NULL,
  resend_after timestamptz NOT NULL,
  attempts integer NOT NULL DEFAULT 0 CHECK (attempts >= 0 AND attempts <= 5),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX pending_registrations_expiry_idx ON pending_registrations(expires_at);

CREATE TABLE sessions (
  token_hash text PRIMARY KEY,
  user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  expires_at timestamptz NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX sessions_user_id_idx ON sessions(user_id);
CREATE INDEX sessions_expiry_idx ON sessions(expires_at);

CREATE TABLE oauth_identities (
  provider text NOT NULL,
  provider_subject text NOT NULL,
  user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (provider, provider_subject)
);
CREATE INDEX oauth_identities_user_idx ON oauth_identities(user_id);

CREATE TABLE installations (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  owner_user_id uuid NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
  name text NOT NULL CHECK (char_length(name) BETWEEN 1 AND 100),
  credential_hash text NOT NULL,
  status text NOT NULL DEFAULT 'offline' CHECK (status IN ('offline', 'online', 'suspended')),
  version text NOT NULL DEFAULT '' CHECK (char_length(version) <= 64),
  last_seen_at timestamptz,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX installations_owner_idx ON installations(owner_user_id);
CREATE INDEX installations_last_seen_idx ON installations(last_seen_at);

CREATE TABLE enrollment_codes (
  code_hash text PRIMARY KEY,
  owner_user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  installation_name text NOT NULL DEFAULT '' CHECK (char_length(installation_name) <= 100),
  expires_at timestamptz NOT NULL,
  consumed_at timestamptz,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX enrollment_codes_expiry_idx ON enrollment_codes(expires_at);
CREATE INDEX enrollment_codes_owner_idx ON enrollment_codes(owner_user_id);

CREATE TABLE request_rate_limits (
  key_hash text PRIMARY KEY,
  window_started_at timestamptz NOT NULL,
  request_count integer NOT NULL DEFAULT 0 CHECK (request_count >= 0),
  blocked_until timestamptz,
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX request_rate_limits_updated_idx ON request_rate_limits(updated_at);

CREATE TABLE servers (
  uuid text PRIMARY KEY CHECK (char_length(uuid) BETWEEN 1 AND 128),
  installation_id uuid NOT NULL REFERENCES installations(id) ON DELETE CASCADE,
  name text NOT NULL CHECK (char_length(name) BETWEEN 1 AND 100),
  software_type text NOT NULL DEFAULT '' CHECK (char_length(software_type) <= 64),
  version text NOT NULL DEFAULT '' CHECK (char_length(version) <= 64),
  status text NOT NULL DEFAULT 'Offline' CHECK (status IN ('Online', 'Offline', 'Starting', 'Stopping', 'Error')),
  port integer NOT NULL DEFAULT 25565 CHECK (port BETWEEN 1 AND 65535),
  ram_min integer NOT NULL DEFAULT 0 CHECK (ram_min >= 0),
  ram_max integer NOT NULL DEFAULT 0 CHECK (ram_max >= ram_min),
  subdomain text UNIQUE CHECK (subdomain IS NULL OR subdomain = lower(subdomain)),
  metadata jsonb NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(metadata) = 'object'),
  last_synced_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX servers_installation_idx ON servers(installation_id);
CREATE INDEX servers_synced_idx ON servers(last_synced_at);

CREATE TABLE server_members (
  id bigserial PRIMARY KEY,
  server_uuid text NOT NULL REFERENCES servers(uuid) ON DELETE CASCADE,
  user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  role text NOT NULL CHECK (char_length(role) BETWEEN 1 AND 64),
  permissions jsonb NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(permissions) = 'object'),
  status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'suspended')),
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (server_uuid, user_id)
);
CREATE INDEX server_members_user_idx ON server_members(user_id, status);

CREATE TABLE agent_commands (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  installation_id uuid NOT NULL REFERENCES installations(id) ON DELETE CASCADE,
  server_uuid text REFERENCES servers(uuid) ON DELETE CASCADE,
  requested_by uuid NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
  action text NOT NULL CHECK (char_length(action) BETWEEN 1 AND 100),
  payload jsonb NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(payload) = 'object'),
  status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'claimed', 'succeeded', 'failed', 'expired')),
  idempotency_key text NOT NULL UNIQUE,
  claimed_at timestamptz,
  expires_at timestamptz NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX agent_commands_poll_idx ON agent_commands(installation_id, status, created_at);
CREATE INDEX agent_commands_server_idx ON agent_commands(server_uuid, created_at DESC);
CREATE INDEX agent_commands_expiry_idx ON agent_commands(expires_at);

CREATE TABLE agent_results (
  command_id uuid PRIMARY KEY REFERENCES agent_commands(id) ON DELETE CASCADE,
  success boolean NOT NULL,
  result jsonb NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(result) = 'object'),
  completed_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE subdomain_reservations (
  subdomain text PRIMARY KEY CHECK (subdomain = lower(subdomain) AND subdomain ~ '^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$'),
  server_uuid text NOT NULL UNIQUE REFERENCES servers(uuid) ON DELETE CASCADE,
  installation_id uuid NOT NULL REFERENCES installations(id) ON DELETE CASCADE,
  state text NOT NULL DEFAULT 'reserved' CHECK (state IN ('reserved', 'published', 'disabled')),
  updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX subdomain_reservations_installation_idx ON subdomain_reservations(installation_id);

CREATE TABLE audit_logs (
  id bigserial PRIMARY KEY,
  user_id uuid REFERENCES users(id) ON DELETE SET NULL,
  installation_id uuid REFERENCES installations(id) ON DELETE SET NULL,
  server_uuid text,
  action text NOT NULL CHECK (char_length(action) BETWEEN 1 AND 100),
  details jsonb NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(details) = 'object'),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX audit_logs_user_idx ON audit_logs(user_id, created_at DESC);
CREATE INDEX audit_logs_installation_idx ON audit_logs(installation_id, created_at DESC);
CREATE INDEX audit_logs_server_idx ON audit_logs(server_uuid, created_at DESC);
