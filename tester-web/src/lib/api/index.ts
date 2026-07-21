export { apiBase, fetchJson, resolveWsBase } from './client';
export * from './util';
export * from './ops';
export * from './media';
export * from './stt';
export * from './vqa';
export * from './grpc';
export * from './analysis-llm';
export * from './process-headers';
export * from './process-audio';
export * from './config';

import { fetchJson } from './client';

export async function checkReady() {
  return fetchJson<Record<string, unknown>>('/ready');
}
