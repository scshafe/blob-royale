import { act, renderHook } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import {
  CAMERA_EDGE_BODY_CENTERS,
  CAMERA_INITIAL_BODY_CENTER,
  CAMERA_MOVED_BODY_CENTER,
  CAMERA_PAN_OFFSET,
  CAMERA_PANNED_CENTER,
  CAMERA_REPLACEMENT_BODY_CENTER,
  CAMERA_WORLD_CENTER,
  CAMERA_WORLD_SIZE,
  cameraConfiguration,
  cameraOptions,
  cameraSessionIdentity,
  cameraSnapshot,
} from './fixtures/simulationCameraFrames';
import { useSimulationCamera } from './useSimulationCamera';

describe('useSimulationCamera', () => {
  it('starts at the world centre before the first body without inventing a target', () => {
    const options = cameraOptions({ snapshot: null });
    const { result } = renderHook(() => useSimulationCamera(options));

    expect(result.current.camera).toEqual({
      center: CAMERA_WORLD_CENTER,
      mode: 'follow',
    });
  });

  it('has an explicit non-rendered origin until configuration supplies the real world', () => {
    const options = cameraOptions({
      configuration: null,
      session: null,
      snapshot: null,
    });
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    expect(result.current.camera.center).toEqual({ x: 0, y: 0 });

    rerender({ ...options, configuration: cameraConfiguration() });

    expect(result.current.camera.center).toEqual(CAMERA_WORLD_CENTER);
  });

  it('does not adopt any entity before this session receives a controller identity', () => {
    const options = cameraOptions({ session: null });
    const { result } = renderHook(() => useSimulationCamera(options));

    expect(result.current.camera.center).toEqual(CAMERA_WORLD_CENTER);
  });

  it('follows the current snapshot on the first render and every movement update', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    expect(result.current.camera.center).toEqual(CAMERA_INITIAL_BODY_CENTER);

    rerender({ ...options, snapshot: cameraSnapshot('moved') });

    expect(result.current.camera.center).toEqual(CAMERA_MOVED_BODY_CENTER);
  });

  it.each(CAMERA_EDGE_BODY_CENTERS)(
    'keeps the player exactly centred at map edge $x,$y without a viewport clamp',
    (center) => {
      const options = cameraOptions({
        snapshot: cameraSnapshot('initial', center),
      });
      const { result } = renderHook(() => useSimulationCamera(options));

      expect(result.current.camera.center).toEqual(center);
    },
  );

  it.each(['bodyless', 'missing'] as const)(
    'retains the latest body centre when the own entity is %s',
    (scenario) => {
      const options = cameraOptions();
      const { result, rerender } = renderHook(useSimulationCamera, {
        initialProps: options,
      });
      rerender({ ...options, snapshot: cameraSnapshot('moved') });

      rerender({ ...options, snapshot: cameraSnapshot(scenario) });

      expect(result.current.camera.center).toEqual(CAMERA_MOVED_BODY_CENTER);
    },
  );

  it('resumes following the same controller on a replacement entity after a bodyless wait', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    rerender({ ...options, snapshot: cameraSnapshot('bodyless') });

    rerender({ ...options, snapshot: cameraSnapshot('replacement') });

    expect(result.current.camera.center).toEqual(
      CAMERA_REPLACEMENT_BODY_CENTER,
    );
  });

  it('freezes manual mode across movement, body loss, replacement, and match restart', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.setMode('manual');
    });

    for (const scenario of [
      'moved',
      'bodyless',
      'replacement',
      'lobby',
    ] as const) {
      rerender({ ...options, snapshot: cameraSnapshot(scenario) });
      expect(result.current.camera).toEqual({
        center: CAMERA_INITIAL_BODY_CENTER,
        mode: 'manual',
      });
    }
  });

  it('selects manual mode and pans from the current view, not from a new body position', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });
    rerender({ ...options, snapshot: cameraSnapshot('moved') });

    expect(result.current.camera).toEqual({
      center: CAMERA_PANNED_CENTER,
      mode: 'manual',
    });
  });

  it('accumulates multiple pans queued in one event without dropping an offset', () => {
    const options = cameraOptions();
    const { result } = renderHook(() => useSimulationCamera(options));
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });

    expect(result.current.camera.center).toEqual({
      x: CAMERA_PANNED_CENTER.x + CAMERA_PAN_OFFSET.x,
      y: CAMERA_PANNED_CENTER.y + CAMERA_PAN_OFFSET.y,
    });
  });

  it('clamps only a manual centre to the inclusive world bounds', () => {
    const options = cameraOptions();
    const { result } = renderHook(() => useSimulationCamera(options));
    act(() => {
      result.current.panByWorldOffset({
        x: -CAMERA_WORLD_SIZE.width,
        y: CAMERA_WORLD_SIZE.height,
      });
    });
    expect(result.current.camera.center).toEqual({
      x: 0,
      y: CAMERA_WORLD_SIZE.height,
    });

    act(() => {
      result.current.panByWorldOffset({
        x: CAMERA_WORLD_SIZE.width,
        y: -CAMERA_WORLD_SIZE.height,
      });
    });
    expect(result.current.camera.center).toEqual({
      x: CAMERA_WORLD_SIZE.width,
      y: 0,
    });
  });

  it('returns immediately to the current body when follow is explicitly selected', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });
    rerender({ ...options, snapshot: cameraSnapshot('moved') });

    act(() => {
      result.current.setMode('follow');
    });

    expect(result.current.camera).toEqual({
      center: CAMERA_MOVED_BODY_CENTER,
      mode: 'follow',
    });
  });

  it('retains a manual centre when follow is selected while bodyless, then resumes on return', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });
    rerender({ ...options, snapshot: cameraSnapshot('bodyless') });
    act(() => {
      result.current.setMode('follow');
    });
    expect(result.current.camera.center).toEqual(CAMERA_PANNED_CENTER);

    rerender({ ...options, snapshot: cameraSnapshot('replacement') });

    expect(result.current.camera.center).toEqual(
      CAMERA_REPLACEMENT_BODY_CENTER,
    );
  });

  it('resets on a new room without adopting the previous room body before connection cleanup', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });

    rerender({ ...options, lobbyId: options.lobbyId + 1 });

    expect(result.current.camera).toEqual({
      center: CAMERA_WORLD_CENTER,
      mode: 'follow',
    });
  });

  it('resets for a new welcome object even when controller and first-entity IDs repeat', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });

    const replacementSession = cameraSessionIdentity();
    expect(replacementSession).toEqual(options.session);
    expect(replacementSession).not.toBe(options.session);
    rerender({ ...options, session: replacementSession, snapshot: null });

    expect(result.current.camera).toEqual({
      center: CAMERA_WORLD_CENTER,
      mode: 'follow',
    });
  });

  it('clears the old view during retry and follows the new session once its body arrives', () => {
    const options = cameraOptions();
    const { result, rerender } = renderHook(useSimulationCamera, {
      initialProps: options,
    });
    act(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });

    rerender({
      ...options,
      configuration: null,
      session: null,
      snapshot: null,
    });
    expect(result.current.camera).toEqual({
      center: { x: 0, y: 0 },
      mode: 'follow',
    });
    rerender({
      ...options,
      session: cameraSessionIdentity(),
      snapshot: cameraSnapshot('replacement'),
    });

    expect(result.current.camera.center).toEqual(
      CAMERA_REPLACEMENT_BODY_CENTER,
    );
  });

  it('gives independent local views over the exact same unchanged snapshot and identity', () => {
    const options = cameraOptions();
    const originalSnapshot = structuredClone(options.snapshot);
    const first = renderHook(() => useSimulationCamera(options));
    const second = renderHook(() => useSimulationCamera(options));

    act(() => {
      first.result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    });

    expect(first.result.current.camera.center).toEqual(CAMERA_PANNED_CENTER);
    expect(second.result.current.camera.center).toEqual(
      CAMERA_INITIAL_BODY_CENTER,
    );
    expect(options.snapshot).toEqual(originalSnapshot);
    expect(Object.keys(first.result.current).sort()).toEqual([
      'camera',
      'panByWorldOffset',
      'setMode',
    ]);
  });

  it.each([Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY])(
    'rejects non-finite pan offset %s instead of producing a corrupt view',
    (invalidOffset) => {
      const options = cameraOptions();
      const { result } = renderHook(() => useSimulationCamera(options));

      expect(() => {
        result.current.panByWorldOffset({ x: invalidOffset, y: 0 });
      }).toThrow(
        'Camera pan requires a configured world and a finite world-space offset.',
      );
      expect(result.current.camera.center).toEqual(CAMERA_INITIAL_BODY_CENTER);
    },
  );

  it('rejects pan before configuration instead of treating the loading origin as a world', () => {
    const options = cameraOptions({
      configuration: null,
      session: null,
      snapshot: null,
    });
    const { result } = renderHook(() => useSimulationCamera(options));

    expect(() => {
      result.current.panByWorldOffset(CAMERA_PAN_OFFSET);
    }).toThrow(
      'Camera pan requires a configured world and a finite world-space offset.',
    );
  });
});
