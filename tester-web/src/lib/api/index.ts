export { apiBase, fetchJson, resolveWsBase } from './client';
export * from './util';
export * from './ops';
export * from './media';
export * from './stt';
export * from './vqa';
export * from './grpc';

import { fetchJson } from './client';

export async function checkReady() {
  return fetchJson<Record<string, unknown>>('/ready');
}
