const STORAGE_KEY = 'voiceqas_ops_token';

export function getOpsToken(): string {
  return localStorage.getItem(STORAGE_KEY) ?? '';
}

export function setOpsToken(token: string) {
  if (token) {
    localStorage.setItem(STORAGE_KEY, token);
  } else {
    localStorage.removeItem(STORAGE_KEY);
  }
}

/** Preenche token dev no primeiro acesso quando VITE_DEFAULT_OPS_TOKEN está definido. */
export function ensureDefaultOpsToken(): void {
  if (getOpsToken()) {
    return;
  }
  const def = import.meta.env.VITE_DEFAULT_OPS_TOKEN as string | undefined;
  if (def) {
    setOpsToken(def);
  }
}

export function opsAuthHeaders(): Record<string, string> {
  const token = getOpsToken();
  return token ? { 'X-Ops-Token': token } : {};
}

export function formatOpsAuthError(message: string): string {
  if (message !== 'unauthorized') {
    return message;
  }
  return 'Não autorizado: use token write (dev-write) ou admin (dev-admin) em Auth ops.';
}
