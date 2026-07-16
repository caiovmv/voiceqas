import { CopyTextButton } from './CopyTextButton';

interface Props {
  loading: boolean;
  analysis: string | null;
  error: string | null;
  model?: string;
  formatOk?: boolean | null;
  formatErrors?: string[];
  attempts?: number | null;
  onAnalyze: () => void;
  disabled?: boolean;
}

export function LlmAnalysisPanel({
  loading,
  analysis,
  error,
  model,
  formatOk,
  formatErrors,
  attempts,
  onAnalyze,
  disabled,
}: Props) {
  const formatWarn = Boolean(analysis && formatOk === false);
  return (
    <section className="panel analysis-llm">
      <div className="analysis-panel-head">
        <h2 className="analysis-step">7. Analisar com agente</h2>
        {analysis ? <CopyTextButton text={analysis} label="Copiar analise" /> : null}
      </div>
      <p className="muted">
        Parecer em 6 secoes com validacao objetiva do DSP. O servidor rejeita e regenera se o
        formato sair do contrato.
      </p>
      <div className="row">
        <button type="button" className="btn primary" disabled={disabled || loading} onClick={onAnalyze}>
          {loading ? 'Analisando...' : 'Analisar com agente'}
        </button>
        {model ? <span className="muted">modelo: {model}</span> : null}
        {attempts != null ? <span className="muted">tentativas: {attempts}</span> : null}
      </div>
      {error ? <p className="error">{error}</p> : null}
      {formatWarn ? (
        <p className="error">
          Formato fora do contrato apos retries
          {formatErrors && formatErrors.length ? `: ${formatErrors.slice(0, 6).join(', ')}` : '.'}{' '}
          A resposta abaixo e a ultima tentativa (nao trate como validacao objetiva confiavel).
        </p>
      ) : null}
      {analysis && <pre className="analysis-llm-out">{analysis}</pre>}
    </section>
  );
}
