// Re-creations of the real app UI (pile card, work frame + top tab) and the video's kitsch layer.
import React from 'react';
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion';
import { contrastText, FONT, H, mix, T, TASKBAR_H, W } from '../theme';
import type { PileSpec } from '../data/roles';
import { AppWindow } from './AppWindow';

export interface Rect { x: number; y: number; w: number; h: number }

export const lerpRect = (a: Rect, b: Rect, t: number): Rect => ({
  x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, w: a.w + (b.w - a.w) * t, h: a.h + (b.h - a.h) * t,
});

export const easeOut = (t: number) => 1 - Math.pow(1 - Math.min(1, Math.max(0, t)), 3);

/** Soft monochrome wallpaper + a generic taskbar. */
export const Desktop: React.FC<{ children?: React.ReactNode; dim?: number }> = ({ children, dim = 0 }) => (
  <AbsoluteFill style={{ fontFamily: FONT, background: 'radial-gradient(1200px 700px at 30% 20%, #f4f4f5 0%, #e4e4e7 55%, #d4d4d8 100%)' }}>
    <AbsoluteFill style={{
      backgroundImage: 'radial-gradient(rgba(0,0,0,.06) 1px, transparent 1px)', backgroundSize: '22px 22px',
    }} />
    {children}
    <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: TASKBAR_H, background: 'rgba(250,250,250,.86)', borderTop: `1px solid ${T.border}`, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 14 }}>
      {['#4f7cff', '#16a34a', '#ea580c', '#7c5cff', '#eab308', '#0ea5e9'].map((c, i) => (
        <div key={i} style={{ width: 30, height: 30, borderRadius: 8, background: c, opacity: 0.85 }} />
      ))}
    </div>
    {dim > 0 && <AbsoluteFill style={{ background: `rgba(9,9,11,${dim})` }} />}
  </AbsoluteFill>
);

export const CARD_W = 300;
export const CARD_H = 228;
export const cardRect = (i: number): Rect => ({ x: W - CARD_W - 40, y: 36 + i * (CARD_H + 26), w: CARD_W, h: CARD_H });

/** The pile card exactly as the app draws it: colored title bar + stacked previews. */
export const PileCard: React.FC<{ pile: PileSpec; style?: React.CSSProperties; hot?: boolean; label?: string }> = ({ pile, style, hot, label }) => {
  const on = contrastText(pile.color);
  const shown = pile.windows.slice(-3);
  return (
    <div style={{
      width: CARD_W, height: CARD_H, background: '#fff', borderRadius: 14, overflow: 'hidden', fontFamily: FONT,
      border: hot ? `2px solid ${T.primary}` : `1px solid ${T.border}`,
      boxShadow: '0 14px 30px rgba(0,0,0,.16), 0 2px 6px rgba(0,0,0,.08)', position: 'relative', ...style,
    }}>
      <div style={{ height: 40, background: pile.color, display: 'flex', alignItems: 'center', padding: '0 14px', gap: 8, color: on }}>
        <span style={{ fontSize: 16 }}>{pile.emoji}</span>
        <span style={{ fontSize: 16, fontWeight: 800, flex: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{pile.name}</span>
        <span style={{ fontSize: 13, fontWeight: 800, padding: '2px 10px', borderRadius: 10, background: on === '#ffffff' ? 'rgba(255,255,255,.22)' : 'rgba(0,0,0,.12)' }}>
          {label ?? pile.windows.length}
        </span>
      </div>
      <div style={{ position: 'relative', height: CARD_H - 40 }}>
        {shown.map((w, k) => {
          const depth = shown.length - 1 - k;  // 0 = front
          return (
            <div key={k} style={{ position: 'absolute', left: 12 + depth * 12, top: 12 + (2 - depth) * 10, transformOrigin: 'top left', transform: 'scale(0.4)' }}>
              <AppWindow kind={w.kind} title={w.title} w={600} h={360} shadow={false} style={{ boxShadow: '0 4px 12px rgba(0,0,0,.18)' }} />
            </div>
          );
        })}
      </div>
    </div>
  );
};

/** "Working on this pile": colored frame around the screen + the top tab. */
export const WorkFrame: React.FC<{ pile: PileSpec; open: number; progress: number; hover?: boolean }> = ({ pile, open, progress, hover }) => {
  const on = contrastText(pile.color);
  const t = easeOut(progress);
  return (
    <AbsoluteFill style={{ pointerEvents: 'none', opacity: t }}>
      <AbsoluteFill style={{ border: `6px solid ${pile.color}` }} />
      <div style={{
        position: 'absolute', top: 0, left: '50%', transform: `translate(-50%, ${(t - 1) * 50}px)`,
        background: hover ? mix(pile.color, '#000000', 0.15) : pile.color, color: on, borderRadius: '0 0 14px 14px',
        padding: '10px 18px 10px 20px', display: 'flex', alignItems: 'center', gap: 14, fontFamily: FONT,
        boxShadow: '0 10px 24px rgba(0,0,0,.22)',
      }}>
        <div style={{ width: 10, height: 10, borderRadius: 5, background: on }} />
        <span style={{ fontSize: 19, fontWeight: 800 }}>{pile.emoji} {pile.name}</span>
        <span style={{ fontSize: 16, opacity: 0.9 }}>작업 중 · 창 {open}개</span>
        <span style={{ fontSize: 15, fontWeight: 800, padding: '4px 12px', borderRadius: 9, background: on === '#ffffff' ? 'rgba(255,255,255,.22)' : 'rgba(0,0,0,.12)' }}>접기 ▴</span>
      </div>
    </AbsoluteFill>
  );
};

/** Outline that flies between a window and its pile card (the app's gather animation). */
export const Ghost: React.FC<{ from: Rect; to: Rect; color: string; t: number }> = ({ from, to, color, t }) => {
  if (t <= 0 || t >= 1) return null;
  const r = lerpRect(from, to, easeOut(t));
  return (
    <div style={{
      position: 'absolute', left: r.x, top: r.y, width: r.w, height: r.h, borderRadius: 10,
      border: `3px solid ${color}`, background: mix(color, '#ffffff', 0.78), opacity: 0.75,
    }} />
  );
};

/** Kitsch sticker: thick outline, offset shadow, slight tilt, springs in. */
export const Sticker: React.FC<{ text: string; bg: string; x: number; y: number; at: number; rotate?: number; size?: number }> = ({
  text, bg, x, y, at, rotate = -6, size = 44,
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const s = spring({ frame: frame - at, fps, config: { damping: 9, stiffness: 180 } });
  if (frame < at) return null;
  return (
    <div style={{
      position: 'absolute', left: x, top: y, transform: `rotate(${rotate}deg) scale(${s})`, transformOrigin: 'center',
      background: bg, color: T.fg, fontFamily: FONT, fontWeight: 900, fontSize: size, padding: '10px 24px',
      borderRadius: 18, border: `4px solid ${T.fg}`, boxShadow: `8px 8px 0 ${T.fg}`, whiteSpace: 'nowrap',
    }}>
      {text}
    </div>
  );
};

/** Bottom-center caption pill. */
export const Caption: React.FC<{ text: string; from: number; to: number }> = ({ text, from, to }) => {
  const frame = useCurrentFrame();
  if (frame < from || frame > to) return null;
  const inT = interpolate(frame, [from, from + 8], [0, 1], { extrapolateRight: 'clamp' });
  const outT = interpolate(frame, [to - 6, to], [1, 0], { extrapolateLeft: 'clamp' });
  const o = Math.min(inT, outT);
  return (
    <div style={{
      position: 'absolute', left: '50%', bottom: TASKBAR_H + 40, transform: `translate(-50%, ${(1 - inT) * 20}px)`,
      opacity: o, background: T.primary, color: T.primaryFg, fontFamily: FONT, fontWeight: 800, fontSize: 40,
      padding: '16px 34px', borderRadius: 999, boxShadow: '0 12px 30px rgba(0,0,0,.25)', whiteSpace: 'nowrap',
    }}>
      {text}
    </div>
  );
};

/** Arrow cursor following keyframes [frame, x, y]; `clicks` are frames with a click ripple. */
export const Cursor: React.FC<{ keys: [number, number, number][]; clicks?: number[] }> = ({ keys, clicks = [] }) => {
  const frame = useCurrentFrame();
  if (frame < keys[0][0]) return null;
  const fs = keys.map((k) => k[0]);
  const ease = (t: number) => t;
  const x = interpolate(frame, fs, keys.map((k) => k[1]), { extrapolateLeft: 'clamp', extrapolateRight: 'clamp', easing: ease });
  const y = interpolate(frame, fs, keys.map((k) => k[2]), { extrapolateLeft: 'clamp', extrapolateRight: 'clamp', easing: ease });
  const click = clicks.find((c) => frame >= c && frame < c + 12);
  const ct = click !== undefined ? (frame - click) / 12 : 0;
  const pressed = click !== undefined && frame - click < 4;
  return (
    <>
      {click !== undefined && (
        <div style={{
          position: 'absolute', left: x - 30 * (0.4 + ct), top: y - 30 * (0.4 + ct), width: 60 * (0.4 + ct), height: 60 * (0.4 + ct),
          borderRadius: '50%', border: `4px solid ${T.fg}`, opacity: 1 - ct,
        }} />
      )}
      <svg width={40} height={48} viewBox="0 0 20 24" style={{ position: 'absolute', left: x, top: y, transform: `scale(${pressed ? 0.88 : 1})`, transformOrigin: 'top left', filter: 'drop-shadow(0 3px 4px rgba(0,0,0,.35))' }}>
        <path d="M1 1 L1 19 L6 14.5 L9.5 22 L12.5 20.6 L9 13.4 L16 13.4 Z" fill="#fff" stroke="#09090b" strokeWidth={1.6} strokeLinejoin="round" />
      </svg>
    </>
  );
};

/** Notification sliding in from the bottom right (generic messenger / mail / alert). */
export const Toast: React.FC<{ icon: string; from: string; text: string; at: number; until: number }> = ({ icon, from, text, at, until }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  if (frame < at || frame > until) return null;
  const s = spring({ frame: frame - at, fps, config: { damping: 14 } });
  const out = interpolate(frame, [until - 8, until], [0, 1], { extrapolateLeft: 'clamp' });
  return (
    <div style={{
      position: 'absolute', right: 40 + CARD_W + 30, bottom: TASKBAR_H + 150, width: 560,
      transform: `translateX(${(1 - s) * 140 + out * 140}px) rotate(${(1 - s) * 4}deg)`, opacity: Math.min(s, 1 - out),
      background: '#fff', borderRadius: 16, border: `1px solid ${T.border}`, fontFamily: FONT,
      boxShadow: '0 20px 40px rgba(0,0,0,.25)', padding: '18px 22px', display: 'flex', gap: 16, alignItems: 'center',
    }}>
      <div style={{ width: 52, height: 52, borderRadius: 14, background: '#fee2e2', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 28 }}>{icon}</div>
      <div style={{ flex: 1 }}>
        <div style={{ fontSize: 17, fontWeight: 800, color: T.fg }}>{from}</div>
        <div style={{ fontSize: 22, fontWeight: 600, color: T.subtleFg, marginTop: 4 }}>{text}</div>
      </div>
    </div>
  );
};

/** Stack Neverflow mark (same as the app icon, src/app/app_icon.cpp): a walnut tile, a cream U-shaped
 *  tray, and three cards stacked inside it — all below the rim, "never overflow". 64-unit grid. */
export const Logo: React.FC<{ size?: number }> = ({ size = 72 }) => (
  <svg width={size} height={size} viewBox="0 0 64 64">
    <defs>
      <linearGradient id="sn-walnut" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stopColor="#7E5536" />
        <stop offset="1" stopColor="#4A2E1D" />
      </linearGradient>
    </defs>
    <rect x="2" y="2" width="60" height="60" rx="14" fill="url(#sn-walnut)" />
    <rect x="20" y="40" width="24" height="6" rx="2" fill="#FFFFFF" />
    <rect x="20" y="32.5" width="24" height="6" rx="2" fill="#BFE3CF" />
    <rect x="20" y="25" width="24" height="6" rx="2" fill="#5B9AC4" />
    <path d="M14 20 V42 A7 7 0 0 0 21 49 H43 A7 7 0 0 0 50 42 V20" fill="none" stroke="#F3E9DE" strokeWidth="4.5"
          strokeLinecap="round" strokeLinejoin="round" />
  </svg>
);

export const BRAND = {
  name: 'Stack Neverflow',
  slogan: 'Tasks stack up, but never overflow.',
  tagline: 'Your windows. Your tasks. One organized desktop.',
};

export const FULL = { W, H };
