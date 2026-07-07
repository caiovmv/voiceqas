import { vi } from 'vitest';

const storage = new Map<string, string>();

Object.defineProperty(globalThis, 'localStorage', {
  value: {
    getItem: (key: string) => storage.get(key) ?? null,
    setItem: (key: string, value: string) => {
      storage.set(key, value);
    },
    removeItem: (key: string) => {
      storage.delete(key);
    },
    clear: () => {
      storage.clear();
    },
  },
  configurable: true,
});

class MockBlob {
  constructor(
    public parts: unknown[],
    public options?: BlobPropertyBag,
  ) {}
}

vi.stubGlobal('Blob', MockBlob);
vi.stubGlobal('URL', {
  createObjectURL: vi.fn(() => 'blob:mock'),
  revokeObjectURL: vi.fn(),
});

const click = vi.fn();
vi.stubGlobal('document', {
  createElement: vi.fn(() => ({
    href: '',
    download: '',
    click,
  })),
});

export { click as mockAnchorClick };
