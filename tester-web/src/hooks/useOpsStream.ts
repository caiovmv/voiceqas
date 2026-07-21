import { useCallback, useEffect, useRef, useState } from 'react';
import { connectOpsStream } from '../lib/api';
import type { OpsEvent, WindowMetrics } from '../lib/types';

export const OPS_WS_RECONNECT_MS = 10_000;

interface UseOpsStreamOptions {
  filterSessionId?: string;
  onVqaWindow: (sessionId: string, metrics: WindowMetrics) => void;
  onSttEvent?: (sessionId: string, event: OpsEvent) => void;
  onAlert?: (sessionId: string, event: OpsEvent) => void;
  onPipelineSnapshot?: (snapshot: import('../lib/types').PipelineSnapshot) => void;
  onStatus: (msg: string) => void;
  autoConnect?: boolean;
  autoReconnect?: boolean;
  reconnectToken?: number;
}

export function useOpsStream({
  filterSessionId,
  onVqaWindow,
  onSttEvent,
  onAlert,
  onPipelineSnapshot,
  onStatus,
  autoConnect = false,
  autoReconnect = true,
  reconnectToken = 0,
}: UseOpsStreamOptions) {
  const [connected, setConnected] = useState(false);
  const handleRef = useRef<ReturnType<typeof connectOpsStream> | null>(null);
  const reconnectTimer = useRef<number | null>(null);
  const manualDisconnect = useRef(false);
  const filterRef = useRef(filterSessionId);
  filterRef.current = filterSessionId;
  const callbacksRef = useRef({ onVqaWindow, onSttEvent, onAlert, onPipelineSnapshot, onStatus });
  callbacksRef.current = { onVqaWindow, onSttEvent, onAlert, onPipelineSnapshot, onStatus };

  const closeSocket = useCallback(() => {
    if (reconnectTimer.current) {
      window.clearTimeout(reconnectTimer.current);
      reconnectTimer.current = null;
    }
    handleRef.current?.close();
    handleRef.current = null;
    setConnected(false);
  }, []);

  const disconnect = useCallback(() => {
    manualDisconnect.current = true;
    closeSocket();
    callbacksRef.current.onStatus('Desconectado');
  }, [closeSocket]);

  const connect = useCallback(() => {
    manualDisconnect.current = false;
    closeSocket();
    handleRef.current = connectOpsStream({
      filterSessionId: filterRef.current,
      onEvent: (ev: OpsEvent) => {
        if (ev.type === 'vqa_window') {
          callbacksRef.current.onVqaWindow(ev.session_id, ev);
        } else if (ev.type === 'stt_final' || ev.type === 'stt_partial') {
          callbacksRef.current.onSttEvent?.(ev.session_id, ev);
        } else if (ev.type === 'alert') {
          callbacksRef.current.onAlert?.(ev.session_id, ev);
        } else if (ev.type === 'pipeline_snapshot' || (!ev.type && 'nodes' in ev && 'links' in ev)) {
          callbacksRef.current.onPipelineSnapshot?.(ev as import('../lib/types').PipelineSnapshot);
        }
      },
      onStatus: (msg) => {
        callbacksRef.current.onStatus(msg);
        if (msg === 'Ops handshake OK') {
          setConnected(true);
        }
        if (autoReconnect && !manualDisconnect.current && msg === 'Ops WebSocket fechado') {
          setConnected(false);
          callbacksRef.current.onStatus(`Reconectando em ${OPS_WS_RECONNECT_MS / 1000}s…`);
          reconnectTimer.current = window.setTimeout(() => connect(), OPS_WS_RECONNECT_MS);
        }
      },
    });
  }, [autoReconnect, closeSocket]);

  useEffect(() => {
    if (!autoConnect) {
      return undefined;
    }
    const debounce = window.setTimeout(() => connect(), 300);
    return () => {
      window.clearTimeout(debounce);
      disconnect();
    };
  }, [autoConnect, connect, disconnect, filterSessionId, reconnectToken]);

  return { connect, disconnect, connected };
}
