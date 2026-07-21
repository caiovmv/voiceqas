import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'node',
    setupFiles: ['./src/test/setup.ts'],
    include: ['src/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html'],
      include: [
        'src/lib/domain/**/*.ts',
        'src/lib/ops.ts',
        'src/lib/auth.ts',
        'src/lib/g711.ts',
        'src/lib/api/util.ts',
      ],
      thresholds: {
        lines: 70,
      },
    },
  },
});
