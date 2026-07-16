import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, test } from '@playwright/test';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../..');

const WAVS = [
  'chamada_c1f8e735-b04c-4b5b-9dd0-5d380d6be330.wav',
  'chamada_nespresso_cliente_testando.wav',
];

const STUBS = new Set(['yeah', 'yeah.', 'e', 'e.', 'ora', 'ora.', 'ok', 'ok.']);

function plainText(fromChunks: string | null | undefined): string {
  return (fromChunks ?? '').replace(/\s+/g, ' ').trim();
}

for (const wavName of WAVS) {
  test(`analysis lab STT before/after — ${wavName}`, async ({ page }) => {
    const wavPath = path.join(repoRoot, wavName);
    await page.goto('/#analysis');
    await expect(page.getByRole('heading', { name: 'Analise — lab de pipeline', level: 1 })).toBeVisible();

    await page.getByTestId('analysis-load-wav').setInputFiles(wavPath);
    await expect(page.getByTestId('analysis-audio-ready')).toBeVisible({ timeout: 60_000 });
    await expect(page.getByTestId('analysis-process')).toBeEnabled();

    await page.getByTestId('analysis-process').click();
    await expect(page.getByTestId('analysis-process')).toBeDisabled();

    // Wait until both STT boxes have meta (ms) — processing can take minutes.
    await expect(page.getByTestId('stt-before-text-meta')).toBeVisible({ timeout: 300_000 });
    await expect(page.getByTestId('stt-after-text-meta')).toBeVisible({ timeout: 300_000 });

    const before = plainText(await page.getByTestId('stt-before-text').innerText());
    const after = plainText(await page.getByTestId('stt-after-text').innerText());

    // Strip meta line (model · ms · N palavras)
    const beforeBody = before.replace(/\n.*parakeet.*$/i, '').replace(/\n.*whisper.*$/i, '').trim();
    const afterBody = after.replace(/\n.*parakeet.*$/i, '').replace(/\n.*whisper.*$/i, '').trim();

    expect(beforeBody.length, `before too short: ${beforeBody}`).toBeGreaterThan(80);
    expect(afterBody.length, `after too short: ${afterBody}`).toBeGreaterThan(80);
    expect(STUBS.has(afterBody.toLowerCase()), `after stub: ${afterBody}`).toBe(false);
    expect(STUBS.has(beforeBody.toLowerCase()), `before stub: ${beforeBody}`).toBe(false);
  });
}