# -*- coding: utf-8 -*-
"""Generate hardened analysis_routes.cpp (UTF-8)."""
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "src" / "server" / "routes" / "analysis_routes.cpp"

SYS = """Voce e engenheiro de som senior (VoIP/SIP + ASR) no VoiceQAS Analysis Lab.
Saida = SOMENTE as 6 secoes com titulos EXATOS do contrato (copia literal).
Config != resultado. Numeros so de objective/STT/VQA. PT-BR tecnico. Sem marketing.
"""

TITLES = """### 1. Diagnostico
### 2. Configuracao do strip
### 3. Validacao objetiva do DSP
### 4. Comparativo STT / presenca
### 5. Sugestoes de mix
### 6. Plano de melhoria continua"""

# Accented contract titles (what we show the model)
TITLES_PT = """### 1. Diagnóstico
### 2. Configuração do strip
### 3. Validação objetiva do DSP
### 4. Comparativo STT / presença
### 5. Sugestões de mix
### 6. Plano de melhoria contínua"""

PREFIX = f"""INSTRUÇÕES OBRIGATÓRIAS (Analysis Lab VoiceQAS) -- leia antes dos dados.

## Método (CRÍTICO)
Configuração != resultado. Knobs do strip são HIPÓTESE, não prova de eficácia.
Afirme melhora/piora SÓ com números em `objective`, VQA ou textos STT.
FATO | HIPÓTESE | RECOMENDAÇÃO.

## PROIBIDO
- Renomear seções / inventar "Pontos Fortes", "Avaliação Geral", "Diagnóstico da Qualidade..."
- "voz otimizada", "compressão ideal", "AGC garante nível ideal", "EQ melhorou inteligibilidade"
- Tratar stt_token_f1_after_vs_before como WER ou "melhorou drasticamente a precisão"
  (F1 alto = texto depois ~ igual ao LEGACY; NÃO é ganho absoluto de ASR)
- Inventar PESQ, STOI, MOS, LUFS, WER, SDR, Sibilance, Formantes, AEC no strip
- Tratar silence_ratio~1 / score baixo no fim como "falha do mix" (é gate/presença)

## Pipeline VoiceQAS
decode -> NR -> HPF -> EQ -> DeEss -> Comp -> Lim -> AGC -> VQA|STT (Silero turns).

## Controles (cite só estes)
strip.nr/hpf/eq/deesser/compressor/limiter/agc, Silero, Primary, require_stt_ready,
simulate_trunk, gate (stt_ready_threshold, min_snr_db, max_silence_ratio_for_ready, ...).

## Formato -- COPIE OS TÍTULOS LITERALMENTE (markdown H3)
{TITLES_PT}

Na seção 3: tabela obrigatória com header:
| Métrica | Antes | Depois | Interpretação |
e linha final: Indisponível neste Lab: ... (use objective.unavailable)

## Exemplo de esqueleto (preencha com dados reais; NÃO invente métricas)
### 1. Diagnóstico
- Clip curto; after ready_ratio X vs before Y (fato de objective).
- Sem juízo de eficácia de EQ/comp.

### 2. Configuração do strip
- HIPÓTESE: NR off; HPF 80; EQ mild; DeEss off; Comp ratio 2; Lim -1; AGC -18.
- (Descreva o que o strip tenta fazer; não diga que "melhorou a voz".)

### 3. Validação objetiva do DSP
| Métrica | Antes | Depois | Interpretação |
| snr_db_mean | 34.7 | 35.0 | Delta pequeno em objective |
| stt_token_f1_after_vs_before | - | 0.98 | Overlap vs LEGACY (não WER) |
Indisponível neste Lab: LUFS, PESQ, STOI, WER, MOS, SDR, Sibilance Index, Formantes F1-F3

### 4. Comparativo STT / presença
- Cite 1-3 trechos before.stt.text vs after.stt.text.
- F1 alto => similar ao LEGACY, não "melhorou drasticamente".

### 5. Sugestões de mix
- 1-3 mudanças priorizadas ligadas a gaps da tabela/STT.

### 6. Plano de melhoria contínua
- 1-3 A/B: hipótese, mudança, métrica EXISTENTE (token_f1, SNR, clipping, ready%).

---
CONTEXTO
- before = LEGACY; after = mix atual; objective = agregados oficiais da seção 3

DADOS JSON:
"""

SUFFIX = f"""

TAREFA: responda AGORA somente com as 6 seções e títulos EXATOS do contrato.
Nenhum preâmbulo. Inclua a tabela da seção 3.
Títulos: 
{TITLES_PT}
"""

RETRY = f"""FORMATO INVÁLIDO. Reescreva a análise INTEIRA do ZERO.

Regras absolutas:
1) Use EXATAMENTE estes títulos H3 (cópia literal, sem adicionar palavras):
{TITLES_PT}
2) Seção 3 DEVE ter tabela com header: | Métrica | Antes | Depois | Interpretação |
3) PROIBIDO: "Pontos Fortes", "Pontos Fracos", "Avaliação Geral", "Diagnóstico da Qualidade", renomear seções.
4) stt_token_f1_after_vs_before = overlap lexical vs LEGACY; NÃO é WER; NÃO diga que a precisão "melhorou drasticamente" só por causa do F1.
5) Não invente PESQ/STOI/LUFS/WER. Liste Indisponível neste Lab.
6) Silêncio no fim (silence_ratio~1) = gate/presença, não falha do mix.

Responda só com as 6 seções.
"""

SYS_PT = """Você é engenheiro de som sênior (VoIP/SIP + ASR) no VoiceQAS Analysis Lab.
Saída = SOMENTE as 6 seções com títulos EXATOS do contrato (cópia literal).
Config != resultado. Números só de objective/STT/VQA. PT-BR técnico. Sem marketing.
"""


def main() -> None:
    cpp = f'''#include "voiceqas/server/routes/register_routes.hpp"

#include "voiceqas/ops/external_ai_config.hpp"
#include "voiceqas/ops/external_ai_service.hpp"
#include "voiceqas/server/auth.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace voiceqas::routes {{

namespace {{

constexpr int kAnalysisMaxAttempts = 3;

constexpr const char* kAnalysisSystemPrompt = R"PROMPT(
{SYS_PT})PROMPT";

constexpr const char* kAnalysisUserEnvelopePrefix = R"ENV(
{PREFIX})ENV";

constexpr const char* kAnalysisUserEnvelopeSuffix = R"ENV(
{SUFFIX})ENV";

constexpr const char* kAnalysisRetryUserPrompt = R"PROMPT(
{RETRY})PROMPT";

std::string to_lower_ascii(std::string_view in) {{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {{
        out.push_back(static_cast<char>(std::tolower(c)));
    }}
    return out;
}}

bool contains_ci(std::string_view hay, std::string_view needle) {{
    return to_lower_ascii(hay).find(to_lower_ascii(needle)) != std::string::npos;
}}

struct FormatCheck {{
    bool ok = false;
    std::vector<std::string> errors;
}};

FormatCheck check_analysis_format(std::string_view text) {{
    FormatCheck r;
    static constexpr std::array<const char*, 6> kRequired = {{
        "### 1. Diagnóstico",
        "### 2. Configuração do strip",
        "### 3. Validação objetiva do DSP",
        "### 4. Comparativo STT / presença",
        "### 5. Sugestões de mix",
        "### 6. Plano de melhoria contínua",
    }};
    static constexpr std::array<const char*, 6> kRequiredAlt = {{
        "### 1. Diagnostico",
        "### 2. Configuracao do strip",
        "### 3. Validacao objetiva do DSP",
        "### 4. Comparativo STT / presenca",
        "### 5. Sugestoes de mix",
        "### 6. Plano de melhoria continua",
    }};

    for (std::size_t i = 0; i < kRequired.size(); ++i) {{
        if (text.find(kRequired[i]) == std::string_view::npos
            && text.find(kRequiredAlt[i]) == std::string_view::npos) {{
            r.errors.push_back(std::string("missing_heading:") + kRequiredAlt[i]);
        }}
    }}

    const bool header_ok = contains_ci(text, "| metrica | antes | depois")
        || contains_ci(text, "| métrica | antes | depois");
    if (!header_ok) {{
        r.errors.push_back("missing_objective_table");
    }}
    if (!contains_ci(text, "indisponivel") && !contains_ci(text, "indisponível")) {{
        r.errors.push_back("missing_unavailable_line");
    }}

    static constexpr std::array<const char*, 6> kBanned = {{
        "pontos fortes",
        "pontos fracos",
        "avaliacao geral",
        "diagnostico da qualidade",
        "recomendacao final",
        "analise de qualidade",
    }};
    for (const char* b : kBanned) {{
        if (contains_ci(text, b)) {{
            r.errors.push_back(std::string("banned_phrase:") + b);
        }}
    }}

    if (contains_ci(text, "stt_token_f1") && contains_ci(text, "wer")) {{
        r.errors.push_back("token_f1_called_wer");
    }}
    if (contains_ci(text, "melhorou drasticamente")) {{
        r.errors.push_back("token_f1_overclaimed");
    }}

    r.ok = r.errors.empty();
    return r;
}}

}}  // namespace

void register_analysis_routes(httplib::Server& server, const RouteContext& /*ctx*/) {{
    server.Post("/v1/analysis/llm", [](const httplib::Request& req, httplib::Response& res) {{
        if (ops_auth_required() && !check_ops_write_auth(req)) {{
            res.status = 401;
            res.set_content("{{\\"error\\":\\"unauthorized\\"}}", "application/json");
            return;
        }}
        try {{
            const auto body = nlohmann::json::parse(req.body.empty() ? "{{}}" : req.body);
            const std::string user_prompt =
                std::string(kAnalysisUserEnvelopePrefix) + body.dump(2) +
                kAnalysisUserEnvelopeSuffix;

            std::string err;
            std::string content;
            FormatCheck check;
            int attempts_used = 0;

            for (int attempt = 0; attempt < kAnalysisMaxAttempts; ++attempt) {{
                std::string attempt_prompt = user_prompt;
                if (attempt > 0) {{
                    attempt_prompt = std::string(kAnalysisRetryUserPrompt);
                    attempt_prompt += "\\nErros da tentativa anterior:\\n";
                    for (const auto& e : check.errors) {{
                        attempt_prompt += "- ";
                        attempt_prompt += e;
                        attempt_prompt += "\\n";
                    }}
                    attempt_prompt +=
                        "\\nUse o JSON (objective/before/after/mix) da conversa anterior e "
                        "reescreva do zero.\\n";
                }}

                content = ops::ExternalAiService::instance().chat(
                    kAnalysisSystemPrompt, attempt_prompt, &err);
                attempts_used = attempt + 1;
                if (content.empty()) {{
                    break;
                }}
                check = check_analysis_format(content);
                if (check.ok) {{
                    break;
                }}
            }}

            if (content.empty()) {{
                res.status = 502;
                res.set_content(
                    nlohmann::json{{{{"error", err.empty() ? "LLM unavailable" : err}}}}.dump(),
                    "application/json");
                return;
            }}

            nlohmann::json out{{
                {{"ok", check.ok}},
                {{"analysis", content}},
                {{"model", ops::external_ai_config().model}},
                {{"format_ok", check.ok}},
                {{"attempts", attempts_used}},
                {{"format_errors", check.errors}},
            }};
            if (!check.ok) {{
                out["error"] =
                    "LLM response failed format contract after retries; see format_errors";
                res.status = 422;
            }}
            res.set_content(out.dump(), "application/json");
        }} catch (const std::exception& e) {{
            res.status = 400;
            res.set_content(nlohmann::json{{{{"error", e.what()}}}}.dump(), "application/json");
        }}
    }});
}}

}}  // namespace voiceqas::routes
'''
    # Fix unauthorized JSON string — avoid over-escaped mess
    cpp = cpp.replace(
        'res.set_content("{{\\"error\\":\\"unauthorized\\"}}", "application/json");',
        'res.set_content(R"({"error":"unauthorized"})", "application/json");',
    )
    OUT.write_text(cpp, encoding="utf-8", newline="\n")
    # Fix UTF-16 if Write tool later corrupts; this path is python write = utf-8
    raw = OUT.read_bytes()
    if raw[:2] == b"\xff\xfe" or (len(raw) > 3 and raw[1] == 0):
        OUT.write_text(OUT.read_text(encoding="utf-16"), encoding="utf-8", newline="\n")
    print("wrote", OUT, OUT.stat().st_size)
    t = OUT.read_text(encoding="utf-8")
    assert "check_analysis_format" in t
    assert "Diagnóstico" in t
    assert "format_ok" in t


if __name__ == "__main__":
    main()
