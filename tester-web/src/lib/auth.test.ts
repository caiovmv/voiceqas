import { beforeEach, describe, expect, it } from 'vitest';
import {
  formatOpsAuthError,
  getOpsToken,
  opsAuthHeaders,
  setOpsToken,
} from './auth';

describe('auth', () => {
  beforeEach(() => {
    setOpsToken('');
  });

  it('stores and returns ops token', () => {
    setOpsToken('dev-write');
    expect(getOpsToken()).toBe('dev-write');
    setOpsToken('');
    expect(getOpsToken()).toBe('');
  });

  it('adds X-Ops-Token header when configured', () => {
    setOpsToken('secret');
    expect(opsAuthHeaders()).toEqual({ 'X-Ops-Token': 'secret' });
    setOpsToken('');
    expect(opsAuthHeaders()).toEqual({});
  });

  it('formats unauthorized message for operators', () => {
    expect(formatOpsAuthError('unauthorized')).toContain('dev-write');
    expect(formatOpsAuthError('server error')).toBe('server error');
  });
});
