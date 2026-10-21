// Same tokens as the app (src/ui/theme.h): shadcn zinc neutrals + Notion-style pile colors,
// plus a few vivid "sticker" colors for the kitsch layer of the video.
export const T = {
  bg: '#ffffff',
  fg: '#09090b',
  border: '#e4e4e7',
  borderStrong: '#a1a1aa',
  muted: '#f4f4f5',
  mutedFg: '#71717a',
  subtleFg: '#52525b',
  primary: '#18181b',
  primaryFg: '#fafafa',
  destructive: '#dc2626',
};

export const PILE = {
  charcoal: '#37352f',
  gray: '#787774',
  brown: '#9f6b53',
  orange: '#d9730d',
  yellow: '#cb912f',
  green: '#448361',
  blue: '#337ea9',
  purple: '#9065b0',
  pink: '#c14c8a',
  red: '#d44c47',
} as const;

export const STICKER = {
  yellow: '#ffe14d',
  pink: '#ff7ab6',
  lime: '#b8f35e',
  sky: '#7dd3fc',
  orange: '#ffb15c',
};

export const FONT = "Pretendard, 'Segoe UI Emoji', 'Malgun Gothic', sans-serif";
export const MONO = "'Cascadia Mono', Consolas, 'Courier New', monospace";

export const W = 1920;
export const H = 1080;
export const TASKBAR_H = 56;

export const contrastText = (hex: string) => {
  const n = parseInt(hex.slice(1), 16);
  const r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255;
  return 0.2126 * r + 0.7152 * g + 0.0722 * b > 160 ? T.fg : '#ffffff';
};

export const mix = (hexA: string, hexB: string, t: number) => {
  const a = parseInt(hexA.slice(1), 16), b = parseInt(hexB.slice(1), 16);
  const ch = (s: number) => Math.round(((a >> s) & 255) + (((b >> s) & 255) - ((a >> s) & 255)) * t);
  return `rgb(${ch(16)},${ch(8)},${ch(0)})`;
};
