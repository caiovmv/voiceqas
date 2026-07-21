import { fetchJson } from './client';
import { opsAuthHeaders } from '../auth';

export type ChannelTransport = 'sip_trunk' | 'websocket' | 'webrtc' | 'rest' | 'grpc';

export interface ChannelProfile {
  id: string;
  label: string;
  transport: ChannelTransport;
  enabled: boolean;
  inherit_pipeline: boolean;
  signaling?: 'pending' | 'n/a';
  media?: {
    preferred_ingress_codec?: string;
    remote_host?: string;
    remote_port?: number;
  };
  pipeline_override?: Record<string, unknown>;
  stt_override?: {
    require_stt_ready?: boolean;
    diarization_focus_primary?: boolean;
  };
  has_override?: boolean;
  updated_at?: string;
}

export interface PipelineDefaultResponse {
  audio: {
    enhancement: { enabled: boolean; wet_dry: number };
    strip: Record<string, unknown>;
  };
  stt: {
    require_stt_ready: boolean;
    diarization_focus_primary: boolean;
    language?: string;
    default_model?: string;
  };
}

export interface ChannelsListResponse {
  status: string;
  default_channel_id: string;
  pipeline_default: PipelineDefaultResponse;
  channels: ChannelProfile[];
}

function configHeaders(): Record<string, string> {
  return { ...opsAuthHeaders(), 'Content-Type': 'application/json' };
}

export async function fetchPipelineDefault(): Promise<PipelineDefaultResponse> {
  return fetchJson<PipelineDefaultResponse>('/v1/config/pipeline-default', {
    headers: opsAuthHeaders(),
  });
}

export async function savePipelineDefault(body: PipelineDefaultResponse): Promise<{ status: string }> {
  return fetchJson('/v1/config/pipeline-default', {
    method: 'PUT',
    headers: configHeaders(),
    body: JSON.stringify(body),
  });
}

export async function fetchChannels(): Promise<ChannelsListResponse> {
  return fetchJson<ChannelsListResponse>('/v1/config/channels', { headers: opsAuthHeaders() });
}

export async function createChannel(body: Partial<ChannelProfile> & { id: string }): Promise<unknown> {
  return fetchJson('/v1/config/channels', {
    method: 'POST',
    headers: configHeaders(),
    body: JSON.stringify(body),
  });
}

export async function updateChannel(id: string, body: Partial<ChannelProfile>): Promise<unknown> {
  return fetchJson(`/v1/config/channels/${encodeURIComponent(id)}`, {
    method: 'PUT',
    headers: configHeaders(),
    body: JSON.stringify({ ...body, id }),
  });
}

export async function deleteChannel(id: string): Promise<{ status: string }> {
  return fetchJson(`/v1/config/channels/${encodeURIComponent(id)}`, {
    method: 'DELETE',
    headers: opsAuthHeaders(),
  });
}
