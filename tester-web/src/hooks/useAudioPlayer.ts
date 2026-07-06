import { useCallback, useEffect, useRef, useState } from 'react';

export function useAudioPlayer() {
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const [playingId, setPlayingId] = useState<string | null>(null);
  const [queueLabel, setQueueLabel] = useState<string | null>(null);

  useEffect(() => {
    const audio = new Audio();
    audioRef.current = audio;
    audio.onended = () => {
      setPlayingId(null);
      setQueueLabel(null);
    };
    return () => {
      audio.pause();
      audio.src = '';
    };
  }, []);

  const stop = useCallback(() => {
    const audio = audioRef.current;
    if (!audio) return;
    audio.pause();
    audio.currentTime = 0;
    setPlayingId(null);
    setQueueLabel(null);
  }, []);

  const playUrl = useCallback(
    async (id: string, url: string, label?: string) => {
      const audio = audioRef.current;
      if (!audio) return;
      stop();
      audio.src = url;
      setPlayingId(id);
      setQueueLabel(label ?? null);
      await audio.play();
    },
    [stop],
  );

  const playAll = useCallback(
    async (items: Array<{ id: string; url: string; label: string }>) => {
      const audio = audioRef.current;
      if (!audio || items.length === 0) return;
      stop();

      for (const item of items) {
        setPlayingId(item.id);
        setQueueLabel(`${item.label} (${items.indexOf(item) + 1}/${items.length})`);
        audio.src = item.url;
        await new Promise<void>((resolve, reject) => {
          const onEnd = () => {
            audio.removeEventListener('ended', onEnd);
            audio.removeEventListener('error', onErr);
            resolve();
          };
          const onErr = () => {
            audio.removeEventListener('ended', onEnd);
            audio.removeEventListener('error', onErr);
            reject(new Error(`Falha ao reproduzir: ${item.label}`));
          };
          audio.addEventListener('ended', onEnd);
          audio.addEventListener('error', onErr);
          audio.play().catch(reject);
        });
        await new Promise((r) => setTimeout(r, 400));
      }

      setPlayingId(null);
      setQueueLabel(null);
    },
    [stop],
  );

  return { playingId, queueLabel, playUrl, playAll, stop };
}
