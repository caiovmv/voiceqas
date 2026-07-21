import { opsAuthHeaders } from '../auth';
import { apiBase } from './client';
import type { AnalysisObjective } from '../domain/analysis-objective';

export interface AnalysisLlmRequest {
  mix: unknown;
  before: unknown;
  after: unknown;
  objective?: AnalysisObjective;
}

export interface AnalysisLlmResponse {
  ok: boolean;
  analysis: string;
  model?: string;
  error?: string;
  format_ok?: boolean;
  attempts?: number;
  format_errors?: string[];
}

export async function requestAnalysisLlm(body: AnalysisLlmRequest): Promise<AnalysisLlmResponse> {
  const res = await fetch(`${apiBase}/v1/analysis/llm`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      ...opsAuthHeaders(),
    },
    body: JSON.stringify(body),
  });
  const json = (await res.json()) as AnalysisLlmResponse;
  // 422 = format failure after retries; still return body so UI can show analysis + errors
  if (!res.ok && res.status !== 422) {
    throw new Error(json.error ?? `HTTP ${res.status}`);
  }
  if (res.status === 422) {
    return { ...json, ok: false, format_ok: false };
  }
  return json;
}
