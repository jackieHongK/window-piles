// One 20-second draft per role: hook → problem (3 cuts) → solution → usage flow → outro.
// 30 fps, 600 frames. All motion is driven by the frame number (deterministic renders).
import React from 'react';
import { AbsoluteFill, interpolate, Sequence, spring, useCurrentFrame, useVideoConfig } from 'remotion';
import { roleById, type Role, type WinSpec } from './data/roles';
import { FONT, STICKER, T, TASKBAR_H, W } from './theme';
import { AppWindow } from './components/AppWindow';
import {
  Caption, cardRect, CARD_H, CARD_W, Cursor, Desktop, easeOut, Ghost, Logo, PileCard, type Rect, Sticker, Toast, WorkFrame,
} from './components/Ui';

const clamp = { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' } as const;

const RECTS_A: Rect[] = [
  { x: 80, y: 70, w: 820, h: 540 },
  { x: 560, y: 170, w: 860, h: 580 },
  { x: 260, y: 390, w: 760, h: 520 },
];
const RECTS_B: Rect[] = [
  { x: 120, y: 110, w: 860, h: 560 },
  { x: 700, y: 80, w: 780, h: 540 },
  { x: 380, y: 420, w: 820, h: 500 },
];
const CHAOS: (Rect & { r: number })[] = [
  { x: 60, y: 60, w: 760, h: 500, r: -4 },
  { x: 900, y: 40, w: 820, h: 520, r: 3 },
  { x: 300, y: 300, w: 800, h: 520, r: 2 },
  { x: 1050, y: 380, w: 780, h: 500, r: -3 },
  { x: 120, y: 520, w: 720, h: 440, r: 5 },
  { x: 620, y: 180, w: 760, h: 500, r: -2 },
  { x: 1180, y: 120, w: 640, h: 440, r: 4 },
  { x: 500, y: 470, w: 820, h: 480, r: -5 },
];
const PROBLEM_RECTS: Rect[] = [
  { x: 840, y: 110, w: 760, h: 480 },
  { x: 1060, y: 290, w: 700, h: 460 },
  { x: 920, y: 500, w: 680, h: 420 },
];

const allWindows = (role: Role): WinSpec[] => role.piles.flatMap((p) => p.windows);
const center = (r: Rect) => [r.x + r.w / 2, r.y + r.h / 2] as const;

const Win: React.FC<{ spec: WinSpec; r: Rect; opacity?: number; scale?: number; rotate?: number; extra?: React.CSSProperties }> = ({
  spec, r, opacity = 1, scale = 1, rotate = 0, extra,
}) => (
  <div style={{ position: 'absolute', left: r.x, top: r.y, opacity, transform: `scale(${scale}) rotate(${rotate}deg)`, transformOrigin: 'center', ...extra }}>
    <AppWindow kind={spec.kind} title={spec.title} w={r.w} h={r.h} />
  </div>
);

// ---------------------------------------------------------------------------------------------
// ① Hook (0–90): windows pour in → "다음에… 뭐 해야 하지?"
// ---------------------------------------------------------------------------------------------
const Hook: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const wins = allWindows(role).slice(0, CHAOS.length);
  const dim = interpolate(f, [40, 52], [0, 0.62], clamp);
  const count = Math.round(interpolate(f, [8, 44], [0, role.openCount], clamp));
  const l1 = spring({ frame: f - 44, fps, config: { damping: 12 } });
  const l2 = spring({ frame: f - 54, fps, config: { damping: 10, stiffness: 160 } });
  const chip = spring({ frame: f - 6, fps, config: { damping: 14 } });
  return (
    <Desktop>
      {wins.map((w, i) => {
        const s = spring({ frame: f - i * 5, fps, config: { damping: 13, stiffness: 140 } });
        const c = CHAOS[i];
        return <Win key={i} spec={w} r={{ ...c, y: c.y - (1 - s) * 90 }} opacity={s} scale={0.6 + 0.4 * s} rotate={c.r * s} />;
      })}
      {/* dim + blur the chaos so the headline always reads cleanly */}
      <AbsoluteFill style={{ background: `rgba(9,9,11,${dim})`, backdropFilter: `blur(${dim * 14}px)` }} />
      <div style={{
        position: 'absolute', left: 60, top: 44, transform: `translateY(${(1 - chip) * -40}px)`, opacity: chip,
        background: T.primary, color: T.primaryFg, fontFamily: FONT, fontWeight: 800, fontSize: 30, padding: '12px 26px', borderRadius: 999,
      }}>
        📌 {role.label} {role.person} · {role.when}
      </div>
      <div style={{
        position: 'absolute', right: 60, top: 44, background: '#fff', color: T.fg, fontFamily: FONT, fontWeight: 900, fontSize: 30,
        padding: '12px 26px', borderRadius: 999, border: `3px solid ${T.fg}`,
      }}>
        열린 창 <span style={{ color: T.destructive }}>{count}</span>개
      </div>
      <div style={{ position: 'absolute', left: 0, right: 0, top: 360, textAlign: 'center', fontFamily: FONT, color: '#fff' }}>
        <div style={{ fontSize: 96, fontWeight: 800, opacity: l1, transform: `translateY(${(1 - l1) * 30}px)` }}>다음에…</div>
        <div style={{ display: 'inline-block', position: 'relative', marginTop: 10, transform: `scale(${0.6 + 0.4 * l2}) rotate(${(1 - l2) * -6}deg)`, opacity: l2 }}>
          <div style={{ position: 'absolute', left: -28, right: -28, top: 18, bottom: 8, background: STICKER.yellow, borderRadius: 18, border: `5px solid ${T.fg}`, boxShadow: `10px 10px 0 ${T.fg}`, transform: 'rotate(-1.5deg)' }} />
          <span style={{ position: 'relative', fontSize: 140, fontWeight: 900, color: T.fg, padding: '0 10px' }}>뭐 해야 하지?</span>
        </div>
      </div>
      <Sticker text="🤯" bg={STICKER.pink} x={1440} y={560} at={64} rotate={10} size={72} />
    </Desktop>
  );
};

// ---------------------------------------------------------------------------------------------
// ② Problem: three quick cuts (50 frames each)
// ---------------------------------------------------------------------------------------------
const ProblemFrame: React.FC<{ index: number; title: string; line: string; sticker: string; color: string; children: React.ReactNode }> = ({
  index, title, line, sticker, color, children,
}) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = spring({ frame: f, fps, config: { damping: 14 } });
  const bubble = spring({ frame: f - 14, fps, config: { damping: 12 } });
  return (
    <Desktop>
      {children}
      <div style={{ position: 'absolute', left: 90, top: 70, display: 'flex', gap: 10, fontFamily: FONT }}>
        {[0, 1, 2].map((i) => (
          <div key={i} style={{
            padding: '8px 18px', borderRadius: 999, fontSize: 22, fontWeight: 800,
            background: i === index ? T.primary : 'rgba(255,255,255,.8)', color: i === index ? T.primaryFg : T.mutedFg,
            border: `2px solid ${i === index ? T.primary : T.border}`,
          }}>문제 {i + 1}</div>
        ))}
      </div>
      <div style={{
        position: 'absolute', left: 90, top: 190, width: 700, fontFamily: FONT,
        transform: `translateX(${(1 - t) * -60}px)`, opacity: t,
      }}>
        <div style={{ fontSize: 92, fontWeight: 900, color: T.fg, lineHeight: 1.12, whiteSpace: 'pre-line' }}>{title}</div>
        <div style={{
          display: 'inline-block', marginTop: 34, background: '#fff', border: `3px solid ${T.fg}`, borderRadius: 22,
          padding: '14px 24px', fontSize: 36, fontWeight: 700, color: T.subtleFg, transform: `scale(${bubble})`, transformOrigin: 'left center',
        }}>
          “{line}”
        </div>
      </div>
      <Sticker text={sticker} bg={color} x={110} y={640} at={26} rotate={-5} size={58} />
    </Desktop>
  );
};

const ProblemMinimize: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = easeOut(interpolate(f, [4, 24], [0, 1], clamp));
  const tb = { x: W / 2, y: 1080 - TASKBAR_H / 2 };
  const after = interpolate(f, [22, 30], [0, 1], clamp);
  return (
    <ProblemFrame index={0} title={'최소화하면'} line={role.problems[0]} sticker="→ 잊어버림 🫠" color={STICKER.pink}>
      {/* where the windows were: empty dashed outlines with question marks */}
      {PROBLEM_RECTS.slice(0, role.piles[0].windows.length).map((r, i) => (
        <div key={`o${i}`} style={{
          position: 'absolute', left: r.x, top: r.y, width: r.w, height: r.h, borderRadius: 12, opacity: after * 0.9,
          border: `4px dashed ${T.borderStrong}`, display: 'flex', alignItems: 'center', justifyContent: 'center',
          fontFamily: FONT, fontSize: 120, fontWeight: 900, color: T.borderStrong,
        }}>?</div>
      ))}
      {role.piles[0].windows.map((w, i) => {
        const r = PROBLEM_RECTS[i];
        const s = 1 - 0.95 * t;
        const [cx, cy] = center(r);
        return <Win key={i} spec={w} r={r} opacity={1 - t * 0.9}
                    extra={{ transform: `translate(${(tb.x - cx) * t}px, ${(tb.y - cy) * t}px) scale(${s})` }} />;
      })}
      {/* the taskbar swallowed them: "?" bubbles over its icons */}
      {[0, 1, 2].map((i) => {
        const s = spring({ frame: f - 24 - i * 3, fps, config: { damping: 10 } });
        return (
          <div key={`q${i}`} style={{
            position: 'absolute', left: W / 2 - 130 + i * 88, top: 1080 - TASKBAR_H - 70, transform: `scale(${s})`,
            width: 56, height: 56, borderRadius: 28, background: '#fff', border: `3px solid ${T.fg}`, fontFamily: FONT,
            display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 30, fontWeight: 900, color: T.fg,
          }}>?</div>
        );
      })}
    </ProblemFrame>
  );
};

const ProblemDesktops: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const t = easeOut(interpolate(f, [6, 26], [0, 1], clamp));
  const thumbs = [1, 2, 3].map((d) => ({ x: W - 120 - (3 - d) * 182 - 170, y: 70, w: 170, h: 100 }));
  const target = thumbs[2];
  return (
    <ProblemFrame index={1} title={'가상 데스크톱에\n넣으면'} line={role.problems[1]} sticker="→ 안 보이니 또 잊음 👻" color={STICKER.yellow}>
      {thumbs.map((r, i) => {
        const active = i === 0;
        return (
          <div key={i} style={{
            position: 'absolute', left: r.x, top: r.y, width: r.w, height: r.h, borderRadius: 12, boxSizing: 'border-box',
            border: `3px solid ${active ? T.fg : T.border}`, background: active ? '#fff' : 'rgba(255,255,255,.6)',
            fontFamily: FONT, fontSize: 20, fontWeight: 800, color: active ? T.fg : T.mutedFg, padding: 10,
            display: 'flex', alignItems: 'flex-end', overflow: 'hidden',
          }}>
            {i === 2 && t > 0.9 && role.piles[1].windows.map((_, k) => (
              <div key={k} style={{ position: 'absolute', left: 18 + k * 34, top: 12 + k * 10, width: 70, height: 44, borderRadius: 4, background: '#fff', border: `2px solid ${role.piles[1].color}` }} />
            ))}
            <span style={{ position: 'relative' }}>데스크톱 {i + 1}</span>
          </div>
        );
      })}
      {/* the windows shrink into desktop 3: out of sight */}
      {role.piles[1].windows.map((w, i) => {
        const r = PROBLEM_RECTS[i];
        const s = 1 - (1 - 70 / r.w) * t;
        const dx = (target.x + 18 + i * 34 - r.x) * t, dy = (target.y + 12 + i * 10 - r.y) * t;
        return <Win key={i} spec={w} r={r} opacity={1 - interpolate(f, [22, 26], [0, 1], clamp)}
                    extra={{ transformOrigin: 'top left', transform: `translate(${dx}px, ${dy}px) scale(${s})` }} />;
      })}
    </ProblemFrame>
  );
};

const ProblemCrowd: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const wins = allWindows(role);
  const n = 12;
  const shake = f > 20 ? Math.sin(f * 2.1) * 6 : 0;
  return (
    <ProblemFrame index={2} title={'다 띄워 두면'} line={role.problems[2]} sticker="→ 정신없음 🌀" color={STICKER.lime}>
      <div style={{ position: 'absolute', inset: 0, transform: `translate(${shake}px, ${shake * 0.6}px)` }}>
        {Array.from({ length: n }).map((_, i) => {
          const s = spring({ frame: f - i * 2, fps, config: { damping: 14, stiffness: 200 } });
          const r = { x: 820 + ((i * 97) % 420), y: 70 + ((i * 61) % 420), w: 640, h: 420 };
          return <Win key={i} spec={wins[i % wins.length]} r={r} opacity={s} scale={0.7 + 0.3 * s} rotate={((i % 5) - 2) * 2.5} />;
        })}
      </div>
    </ProblemFrame>
  );
};

// ---------------------------------------------------------------------------------------------
// ③ Solution (240–300): windows gather into three pile cards
// ---------------------------------------------------------------------------------------------
const Solution: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const groups = role.piles.map((p, pi) => p.windows.map((w, wi) => ({ w, pi, r: { x: 760 + wi * 150 + pi * 40, y: 120 + pi * 230 + wi * 30, w: 560, h: 360 } })));
  const gather = interpolate(f, [16, 34], [0, 1], clamp);
  const a = spring({ frame: f, fps, config: { damping: 14 } });
  const b = spring({ frame: f - 12, fps, config: { damping: 11 } });
  const logo = spring({ frame: f - 30, fps, config: { damping: 12 } });
  return (
    <Desktop>
      {groups.flat().map(({ w, pi, r }, i) => {
        const to = cardRect(pi);
        const e = easeOut(gather);
        const cur = { x: r.x + (to.x - r.x) * e, y: r.y + (to.y - r.y) * e, w: r.w, h: r.h };
        const s = 1 - (1 - CARD_W / r.w) * e;
        return <Win key={i} spec={w} r={cur} opacity={1 - interpolate(f, [30, 36], [0, 1], clamp)} scale={s}
                    extra={{ transformOrigin: 'top left' }} />;
      })}
      {role.piles.map((p, i) => {
        const s = spring({ frame: f - 30 - i * 3, fps, config: { damping: 12 } });
        const r = cardRect(i);
        return <PileCard key={i} pile={p} style={{ position: 'absolute', left: r.x, top: r.y, transform: `scale(${s})` }} />;
      })}
      <div style={{ position: 'absolute', left: 100, top: 230, fontFamily: FONT, width: 760 }}>
        <div style={{ fontSize: 40, fontWeight: 700, color: T.subtleFg, opacity: a }}>그래서 직접 만들었어요.</div>
        <div style={{ fontSize: 104, fontWeight: 900, color: T.fg, lineHeight: 1.1, marginTop: 16, transform: `scale(${0.85 + 0.15 * b})`, transformOrigin: 'left', opacity: b }}>
          일을 <span style={{ background: STICKER.yellow, padding: '0 12px', borderRadius: 12 }}>‘더미’</span>로<br />접어 두세요
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 18, marginTop: 40, opacity: logo, transform: `translateY(${(1 - logo) * 20}px)` }}>
          <Logo size={64} />
          <div style={{ fontSize: 40, fontWeight: 900, color: T.fg }}>창 더미 <span style={{ color: T.mutedFg, fontWeight: 600, fontSize: 30 }}>Window Piles</span></div>
        </div>
      </div>
    </Desktop>
  );
};

// ---------------------------------------------------------------------------------------------
// ④ Usage flow (300–510): open A → interruption → open B (A folds) → fold B → back to A
// ---------------------------------------------------------------------------------------------
// Beat timing (frames, relative to the start of the flow scene).
const K = {
  clickA: 22,      // open pile A
  toastIn: 50, toastOut: 112,
  clickB: 92,      // switch to pile B (A folds back into its card)
  clickTab: 142,   // fold B from the top tab
  clickA2: 164,    // back to A
  spread: 16, gather: 14,
};

const Flow: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const [A, B, C] = role.piles;
  const rectsA = RECTS_A.slice(0, A.windows.length);
  const rectsB = RECTS_B.slice(0, B.windows.length);
  const cA = cardRect(0), cB = cardRect(1);
  const [ax, ay] = center(cA), [bx, by] = center(cB);
  const fadeIn = (at: number) => interpolate(f, [at, at + 8], [0, 1], clamp);
  const fadeOut = (at: number) => interpolate(f, [at, at + 6], [0, 1], clamp);

  const aOpen1 = K.clickA + K.spread, aOpen2 = K.clickA2 + K.spread;
  const bOpen = K.clickB + 6 + K.spread;
  // window groups
  const showA = (f >= aOpen1 && f < K.clickB) || f >= aOpen2;
  const aIn = f < K.clickB ? fadeIn(aOpen1) : fadeIn(aOpen2);
  const showB = f >= bOpen && f < K.clickTab;
  const bIn = fadeIn(bOpen);
  // cards are hidden while their pile is open
  const cardA = f < K.clickA + 2 ? 1 : f >= K.clickB + K.gather && f < K.clickA2 + 2 ? fadeIn(K.clickB + K.gather) : 0;
  const cardB = f < K.clickB + 4 ? 1 : f >= K.clickTab + K.gather ? fadeIn(K.clickTab + K.gather) : 0;
  // work frame + top tab
  const frameA = f < K.clickB ? fadeIn(aOpen1 - 2) - fadeOut(K.clickB) : fadeIn(aOpen2 - 2);
  const frameB = f >= bOpen - 2 && f < K.clickTab + 8 ? fadeIn(bOpen - 2) - fadeOut(K.clickTab) : 0;
  const tabX = W / 2 + 150, tabY = 22;

  return (
    <Desktop>
      {showA && rectsA.map((r, i) => <Win key={`a${i}`} spec={A.windows[i]} r={r} opacity={aIn} scale={0.96 + 0.04 * aIn} />)}
      {showB && rectsB.map((r, i) => <Win key={`b${i}`} spec={B.windows[i]} r={r} opacity={bIn} scale={0.96 + 0.04 * bIn} />)}

      {/* spread & gather outlines, as in the app */}
      {rectsA.map((r, i) => <Ghost key={`ga${i}`} from={cA} to={r} color={A.color} t={(f - K.clickA) / K.spread} />)}
      {rectsA.map((r, i) => <Ghost key={`gA${i}`} from={r} to={cA} color={A.color} t={(f - K.clickB) / K.gather} />)}
      {rectsB.map((r, i) => <Ghost key={`gb${i}`} from={cB} to={r} color={B.color} t={(f - K.clickB - 6) / K.spread} />)}
      {rectsB.map((r, i) => <Ghost key={`gB${i}`} from={r} to={cB} color={B.color} t={(f - K.clickTab) / K.gather} />)}
      {rectsA.map((r, i) => <Ghost key={`ga2${i}`} from={cA} to={r} color={A.color} t={(f - K.clickA2) / K.spread} />)}

      {[A, B, C].map((p, i) => {
        const r = cardRect(i);
        const op = i === 0 ? cardA : i === 1 ? cardB : 1;
        const hot = (i === 0 && f >= K.clickA - 8 && f < K.clickA + 2) || (i === 1 && f >= K.clickB - 8 && f < K.clickB + 4) ||
                    (i === 0 && f >= K.clickA2 - 8 && f < K.clickA2 + 2);
        return <PileCard key={i} pile={p} hot={hot} style={{ position: 'absolute', left: r.x, top: r.y, opacity: op }} />;
      })}

      {frameA > 0 && <WorkFrame pile={A} open={A.windows.length} progress={frameA} />}
      {frameB > 0 && <WorkFrame pile={B} open={B.windows.length} progress={frameB} hover={f >= K.clickTab - 10 && f < K.clickTab + 2} />}

      <Toast icon={role.interrupt.icon} from={role.interrupt.from} text={role.interrupt.text} at={K.toastIn} until={K.toastOut} />
      {f >= K.clickB + 18 && f < K.clickTab && (
        <Sticker text="하던 일은 여기 그대로! 👉" bg={STICKER.yellow} x={cA.x - 520} y={cA.y + CARD_H / 2 - 40} at={K.clickB + 18} rotate={-3} size={34} />
      )}
      {f >= aOpen2 + 4 && <Sticker text="다시 그 자리 그대로 ✨" bg={STICKER.lime} x={160} y={140} at={aOpen2 + 4} rotate={-4} size={40} />}

      <Caption text="① 더미를 누르면 바로 그 일로" from={2} to={K.toastIn + 2} />
      <Caption text="② 급한 일이 끼어들면, 그 더미를 누르세요" from={K.toastIn + 6} to={K.clickB + 14} />
      <Caption text="③ 하던 일은 카드로 접혀 계속 보여요" from={K.clickB + 18} to={K.clickTab + 6} />
      <Caption text="④ 끝나면 접기 → 하던 그대로 복귀" from={K.clickTab + 10} to={210} />

      <Cursor
        keys={[[0, 980, 760], [K.clickA - 4, ax, ay], [K.toastIn + 10, ax - 60, ay + 40], [K.clickB - 4, bx, by],
               [K.clickB + 20, bx - 40, by + 30], [K.clickTab - 4, tabX, tabY], [K.clickTab + 10, tabX + 60, 220], [K.clickA2 - 4, ax, ay]]}
        clicks={[K.clickA, K.clickB, K.clickTab, K.clickA2]}
      />
    </Desktop>
  );
};

// ---------------------------------------------------------------------------------------------
// ⑤ Outro (510–600)
// ---------------------------------------------------------------------------------------------
const Outro: React.FC<{ role: Role }> = ({ role }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const tag = spring({ frame: f - 22, fps, config: { damping: 12 } });
  const logo = spring({ frame: f - 42, fps, config: { damping: 12 } });
  const totalW = 3 * CARD_W + 2 * 60;
  return (
    <Desktop>
      {role.piles.map((p, i) => {
        const from = cardRect(i);
        const to = { x: (W - totalW) / 2 + i * (CARD_W + 60), y: 210 };
        const t = spring({ frame: f - i * 4, fps, config: { damping: 14 } });
        return <PileCard key={i} pile={p} style={{
          position: 'absolute', left: from.x + (to.x - from.x) * t, top: from.y + (to.y - from.y) * t,
          transform: `rotate(${(i - 1) * 3 * t}deg) scale(${1 + 0.1 * t})`,
        }} />;
      })}
      <div style={{ position: 'absolute', left: 0, right: 0, top: 560, textAlign: 'center', fontFamily: FONT }}>
        <div style={{ fontSize: 92, fontWeight: 900, color: T.fg, opacity: tag, transform: `translateY(${(1 - tag) * 30}px)` }}>
          일은 쌓아 두되, <span style={{ background: STICKER.yellow, padding: '0 14px', borderRadius: 14 }}>잊지는 않게.</span>
        </div>
        <div style={{ display: 'inline-flex', alignItems: 'center', gap: 18, marginTop: 44, opacity: logo, transform: `scale(${0.9 + 0.1 * logo})` }}>
          <Logo size={70} />
          <div style={{ fontSize: 46, fontWeight: 900, color: T.fg }}>창 더미</div>
          <div style={{ fontSize: 30, fontWeight: 600, color: T.mutedFg }}>Window Piles · Windows용</div>
        </div>
      </div>
    </Desktop>
  );
};

export const RoleVideo: React.FC<{ roleId: string }> = ({ roleId }) => {
  const role = roleById(roleId);
  return (
    <AbsoluteFill style={{ background: '#fff' }}>
      <Sequence durationInFrames={90}><Hook role={role} /></Sequence>
      <Sequence from={90} durationInFrames={50}><ProblemMinimize role={role} /></Sequence>
      <Sequence from={140} durationInFrames={50}><ProblemDesktops role={role} /></Sequence>
      <Sequence from={190} durationInFrames={50}><ProblemCrowd role={role} /></Sequence>
      <Sequence from={240} durationInFrames={60}><Solution role={role} /></Sequence>
      <Sequence from={300} durationInFrames={210}><Flow role={role} /></Sequence>
      <Sequence from={510} durationInFrames={90}><Outro role={role} /></Sequence>
    </AbsoluteFill>
  );
};
