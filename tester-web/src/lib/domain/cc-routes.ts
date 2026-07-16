export type CcRoute =
  | 'monitor'
  | 'pipeline'
  | 'asr'
  | 'sessions'
  | 'observability'
  | 'config';

export const CC_ROUTES: { id: CcRoute; label: string; hint: string }[] = [
  { id: 'monitor', label: 'Monitor', hint: 'Overview NOC' },
  { id: 'pipeline', label: 'Pipeline', hint: 'Inbound / Outbound' },
  { id: 'asr', label: 'ASR / STT', hint: 'Recognizer + VAD' },
  { id: 'sessions', label: 'Sessoes', hint: 'Media + historico' },
  { id: 'observability', label: 'Observabilidade', hint: 'RED + APM' },
  { id: 'config', label: 'Configuracao', hint: 'Auth, canais, mix' },
];

export function ccHashFor(route: CcRoute): string {
  return route === 'monitor' ? '#command-center' : `#command-center/${route}`;
}

export function parseCcRoute(hash: string): CcRoute {
  const m = hash.match(/^#command-center(?:\/([a-z]+))?/);
  const sub = m?.[1] as CcRoute | undefined;
  if (sub && CC_ROUTES.some((r) => r.id === sub)) {
    return sub;
  }
  return 'monitor';
}

export function isCommandCenterHash(hash: string): boolean {
  return hash === '#command-center' || hash.startsWith('#command-center/');
}
