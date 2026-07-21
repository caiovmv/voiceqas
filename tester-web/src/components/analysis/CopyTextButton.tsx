import { useCallback, useState } from 'react';

interface Props {
  text: string;
  label?: string;
  className?: string;
}

/** Small clipboard helper for Lab STT / LLM panels. */
export function CopyTextButton({ text, label = 'Copiar', className }: Props) {
  const [copied, setCopied] = useState(false);

  const onCopy = useCallback(async () => {
    const value = text.trim();
    if (!value) return;
    try {
      await navigator.clipboard.writeText(value);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1500);
    } catch {
      /* ignore — clipboard may be denied */
    }
  }, [text]);

  return (
    <button
      type="button"
      className={className ?? 'btn btn-sm'}
      disabled={!text.trim()}
      onClick={() => void onCopy()}
    >
      {copied ? 'Copiado' : label}
    </button>
  );
}
