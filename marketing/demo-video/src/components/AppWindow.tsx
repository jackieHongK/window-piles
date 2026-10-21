// Generic, brand-free application windows drawn with plain shapes.
import React from 'react';
import { FONT, MONO, T } from '../theme';
import type { WindowKind } from '../data/roles';

const KIND_COLOR: Record<WindowKind, string> = {
  board: '#4f7cff', design: '#a259ff', doc: '#3b82f6', chat: '#7c5cff', dashboard: '#14b8a6',
  log: '#3f3f46', sheet: '#16a34a', slides: '#ea580c', ads: '#2563eb', folder: '#eab308',
  pdf: '#dc2626', mail: '#0ea5e9', admin: '#6366f1', crm: '#0891b2', code: '#2563eb',
  terminal: '#18181b', browser: '#64748b', pr: '#8b5cf6', erp: '#0f766e', invoice: '#f97316',
  moodboard: '#ec4899',
};

const Bar: React.FC<{ w: string | number; h?: number; c?: string; r?: number; style?: React.CSSProperties }> = ({
  w, h = 8, c = '#e4e4e7', r = 4, style,
}) => <div style={{ width: w, height: h, background: c, borderRadius: r, ...style }} />;

const Lines: React.FC<{ n: number; c?: string; gap?: number }> = ({ n, c, gap = 10 }) => (
  <div style={{ display: 'flex', flexDirection: 'column', gap }}>
    {Array.from({ length: n }).map((_, i) => (
      <Bar key={i} w={`${[92, 78, 86, 64, 90, 72][i % 6]}%`} c={c} />
    ))}
  </div>
);

const Chart: React.FC<{ color: string; spike?: boolean }> = ({ color, spike }) => {
  const pts = spike
    ? [10, 12, 11, 13, 12, 14, 13, 15, 40, 72, 88]
    : [20, 28, 24, 36, 33, 45, 42, 55, 52, 64, 70];
  const path = pts.map((v, i) => `${(i / (pts.length - 1)) * 100},${100 - v}`).join(' ');
  return (
    <svg viewBox="0 0 100 100" preserveAspectRatio="none" style={{ width: '100%', height: '100%' }}>
      <polyline points={path} fill="none" stroke={color} strokeWidth={2.5} vectorEffect="non-scaling-stroke" />
      <polygon points={`0,100 ${path} 100,100`} fill={color} opacity={0.12} />
    </svg>
  );
};

const Grid: React.FC<{ rows: number; cols: number; head: string; highlight?: number }> = ({ rows, cols, head, highlight }) => (
  <div style={{ display: 'grid', gridTemplateColumns: `28px repeat(${cols}, 1fr)`, border: `1px solid ${T.border}`, fontSize: 11 }}>
    {Array.from({ length: (rows + 1) * (cols + 1) }).map((_, i) => {
      const r = Math.floor(i / (cols + 1)), c = i % (cols + 1);
      const isHead = r === 0, isIdx = c === 0;
      const hl = highlight !== undefined && r === highlight;
      return (
        <div key={i} style={{
          height: 22, borderRight: `1px solid ${T.border}`, borderBottom: `1px solid ${T.border}`,
          background: isHead ? head : isIdx ? T.muted : hl ? '#fef9c3' : '#fff',
          display: 'flex', alignItems: 'center', padding: '0 6px', color: isHead ? '#fff' : T.subtleFg,
        }}>
          {isHead || isIdx ? (isIdx ? (r === 0 ? '' : r) : String.fromCharCode(64 + c)) :
            <Bar w={`${40 + ((r * 7 + c * 13) % 50)}%`} h={6} c={hl ? '#facc15' : '#d4d4d8'} />}
        </div>
      );
    })}
  </div>
);

const Body: React.FC<{ kind: WindowKind; accent: string }> = ({ kind, accent }) => {
  const pad = { padding: 18, height: '100%', boxSizing: 'border-box' as const };
  switch (kind) {
    case 'board':
      return (
        <div style={{ ...pad, display: 'flex', gap: 14 }}>
          {['할 일', '진행 중', '완료'].map((col, ci) => (
            <div key={col} style={{ flex: 1, background: T.muted, borderRadius: 8, padding: 10, display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div style={{ fontSize: 12, fontWeight: 700, color: T.subtleFg }}>{col}</div>
              {Array.from({ length: 4 - ci }).map((_, i) => (
                <div key={i} style={{ background: '#fff', borderRadius: 6, padding: 8, border: `1px solid ${T.border}` }}>
                  <Bar w="80%" h={6} /><Bar w="50%" h={6} c={i % 2 ? accent : '#d4d4d8'} style={{ marginTop: 6 }} />
                </div>
              ))}
            </div>
          ))}
        </div>
      );
    case 'design':
      return (
        <div style={{ display: 'flex', height: '100%' }}>
          <div style={{ width: '16%', borderRight: `1px solid ${T.border}`, padding: 10 }}><Lines n={8} gap={9} /></div>
          <div style={{ flex: 1, background: '#e9e9ec', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '6%' }}>
            {[0, 1].map((i) => (
              <div key={i} style={{ width: '24%', aspectRatio: '9/18', background: '#fff', borderRadius: 14, boxShadow: '0 4px 14px rgba(0,0,0,.12)', padding: 10, display: 'flex', flexDirection: 'column', gap: 8 }}>
                <div style={{ height: '28%', borderRadius: 8, background: i ? '#fde68a' : '#c7d2fe' }} />
                <Lines n={3} gap={7} />
                <div style={{ marginTop: 'auto', height: 22, borderRadius: 11, background: accent }} />
              </div>
            ))}
          </div>
          <div style={{ width: '16%', borderLeft: `1px solid ${T.border}`, padding: 10 }}><Lines n={7} gap={12} /></div>
        </div>
      );
    case 'moodboard':
      return (
        <div style={{ ...pad, display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 10 }}>
          {['#fecaca', '#fde68a', '#bbf7d0', '#bfdbfe', '#e9d5ff', '#fbcfe8', '#d4d4d8', '#fed7aa'].map((c, i) => (
            <div key={i} style={{ background: c, borderRadius: 8, gridRow: i % 3 === 0 ? 'span 2' : undefined }} />
          ))}
        </div>
      );
    case 'doc':
    case 'pdf':
      return (
        <div style={{ ...pad, background: kind === 'pdf' ? '#e4e4e7' : '#fff', display: 'flex', justifyContent: 'center' }}>
          <div style={{ width: '72%', background: '#fff', padding: '5%', boxShadow: kind === 'pdf' ? '0 2px 8px rgba(0,0,0,.12)' : undefined, display: 'flex', flexDirection: 'column', gap: 12 }}>
            <Bar w="55%" h={14} c={T.fg} />
            <Lines n={5} />
            <Bar w="35%" h={10} c={accent} />
            <Lines n={4} />
            {kind === 'pdf' && <div style={{ marginTop: 'auto', alignSelf: 'flex-end', width: '30%', height: 40, border: `2px dashed ${accent}`, borderRadius: 6 }} />}
          </div>
        </div>
      );
    case 'chat':
      return (
        <div style={{ display: 'flex', height: '100%' }}>
          <div style={{ width: '28%', background: '#f4f4f5', padding: 12, display: 'flex', flexDirection: 'column', gap: 10 }}>
            {Array.from({ length: 6 }).map((_, i) => (
              <div key={i} style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                <div style={{ width: 22, height: 22, borderRadius: 11, background: i === 0 ? accent : '#d4d4d8' }} />
                <Bar w="60%" h={7} />
              </div>
            ))}
          </div>
          <div style={{ flex: 1, padding: 16, display: 'flex', flexDirection: 'column', gap: 12, justifyContent: 'flex-end' }}>
            {[0, 1, 2, 3].map((i) => (
              <div key={i} style={{ alignSelf: i % 2 ? 'flex-end' : 'flex-start', width: `${[52, 40, 60, 46][i]}%`, padding: 10, borderRadius: 12, background: i === 3 ? '#fee2e2' : i % 2 ? accent : T.muted }}>
                <Bar w="85%" h={7} c={i % 2 && i !== 3 ? 'rgba(255,255,255,.7)' : i === 3 ? '#f87171' : '#d4d4d8'} />
              </div>
            ))}
            <div style={{ height: 34, borderRadius: 8, border: `1px solid ${T.border}` }} />
          </div>
        </div>
      );
    case 'dashboard':
    case 'ads':
      return (
        <div style={{ ...pad, display: 'flex', flexDirection: 'column', gap: 12 }}>
          <div style={{ display: 'flex', gap: 10 }}>
            {['#', '#', '#', '#'].map((_, i) => (
              <div key={i} style={{ flex: 1, border: `1px solid ${T.border}`, borderRadius: 8, padding: 10 }}>
                <Bar w="50%" h={6} /><Bar w="70%" h={14} c={i === 0 ? accent : T.fg} style={{ marginTop: 8 }} />
              </div>
            ))}
          </div>
          <div style={{ flex: 1, border: `1px solid ${T.border}`, borderRadius: 8, padding: 10 }}>
            <Chart color={accent} spike={kind === 'dashboard'} />
          </div>
          {kind === 'ads' && <Lines n={3} />}
        </div>
      );
    case 'log':
    case 'terminal':
      return (
        <div style={{ ...pad, background: '#0b0b0f', fontFamily: MONO, fontSize: 13, lineHeight: 1.7, color: kind === 'terminal' ? '#86efac' : '#a1a1aa', overflow: 'hidden' }}>
          {(kind === 'terminal'
            ? ['$ npm run dev', '  ready - started server on :3000', '  compiled /search in 412ms', '$ npm test search', '  ✓ query parser (12 ms)', '  ✓ ranks by relevance (8 ms)', '  ✓ handles empty input (3 ms)', '$ _']
            : ['10:41:02 INFO  request /pay 200', '10:41:03 ERROR PaymentTimeout gateway=pg-2', '10:41:03 ERROR PaymentTimeout gateway=pg-2', '10:41:04 WARN  retry 1/3 order=88213', '10:41:05 ERROR NullPointerException at Pay..', '10:41:05 INFO  request /pay 200', '10:41:06 ERROR PaymentTimeout gateway=pg-2']
          ).map((l, i) => (
            <div key={i} style={{ color: l.includes('ERROR') ? '#f87171' : l.includes('WARN') ? '#fbbf24' : undefined, whiteSpace: 'nowrap' }}>{l}</div>
          ))}
        </div>
      );
    case 'code':
      return (
        <div style={{ display: 'flex', height: '100%', background: '#1e1e24', fontFamily: MONO, fontSize: 13 }}>
          <div style={{ width: '18%', background: '#18181d', padding: 12 }}><Lines n={9} c="#3f3f46" /></div>
          <div style={{ flex: 1, padding: 16, display: 'flex', flexDirection: 'column', gap: 9 }}>
            {[[18, '#c084fc'], [42, '#60a5fa'], [30, '#fbbf24'], [55, '#a1a1aa'], [24, '#34d399'], [48, '#60a5fa'], [36, '#f472b6'], [20, '#a1a1aa'], [44, '#fbbf24'], [28, '#60a5fa']].map(([w, c], i) => (
              <div key={i} style={{ display: 'flex', gap: 10, paddingLeft: [0, 1, 2, 2, 3, 2, 1, 1, 2, 0][i] * 22 }}>
                <span style={{ color: '#52525b', width: 18 }}>{i + 12}</span>
                <Bar w={`${w}%`} h={8} c={c as string} />
              </div>
            ))}
          </div>
        </div>
      );
    case 'pr':
      return (
        <div style={{ ...pad, display: 'flex', flexDirection: 'column', gap: 6, fontFamily: MONO, fontSize: 12 }}>
          <div style={{ display: 'flex', gap: 8, marginBottom: 6 }}>
            <div style={{ padding: '4px 10px', borderRadius: 12, background: '#22c55e', color: '#fff', fontFamily: FONT, fontWeight: 700 }}>Open</div>
            <Bar w="40%" h={12} c={T.fg} style={{ alignSelf: 'center' }} />
          </div>
          {Array.from({ length: 10 }).map((_, i) => (
            <div key={i} style={{ height: 20, borderRadius: 3, background: i % 4 === 1 ? '#dcfce7' : i % 4 === 2 ? '#fee2e2' : '#fafafa', display: 'flex', alignItems: 'center', padding: '0 8px' }}>
              <Bar w={`${30 + (i * 17) % 55}%`} h={6} c={i % 4 === 1 ? '#86efac' : i % 4 === 2 ? '#fca5a5' : '#d4d4d8'} />
            </div>
          ))}
        </div>
      );
    case 'sheet':
      return <div style={{ ...pad, padding: 10 }}><Grid rows={14} cols={7} head={accent} highlight={6} /></div>;
    case 'erp':
      return (
        <div style={{ display: 'flex', height: '100%' }}>
          <div style={{ width: '20%', background: '#134e4a', padding: 12, display: 'flex', flexDirection: 'column', gap: 12 }}>
            {Array.from({ length: 7 }).map((_, i) => <Bar key={i} w="80%" h={8} c={i === 2 ? '#5eead4' : '#2d6b66'} />)}
          </div>
          <div style={{ flex: 1, padding: 10 }}><Grid rows={13} cols={5} head="#0f766e" highlight={4} /></div>
        </div>
      );
    case 'slides':
      return (
        <div style={{ display: 'flex', height: '100%', background: '#f4f4f5' }}>
          <div style={{ width: '18%', padding: 10, display: 'flex', flexDirection: 'column', gap: 10 }}>
            {[0, 1, 2, 3].map((i) => <div key={i} style={{ aspectRatio: '16/9', background: '#fff', borderRadius: 4, border: i === 1 ? `2px solid ${accent}` : `1px solid ${T.border}` }} />)}
          </div>
          <div style={{ flex: 1, padding: '4%', display: 'flex', alignItems: 'center' }}>
            <div style={{ width: '100%', aspectRatio: '16/9', background: '#fff', borderRadius: 6, boxShadow: '0 2px 10px rgba(0,0,0,.08)', padding: '5%', display: 'flex', flexDirection: 'column', gap: 14 }}>
              <Bar w="50%" h={16} c={T.fg} />
              <div style={{ flex: 1, display: 'flex', alignItems: 'flex-end', gap: '6%' }}>
                {[40, 65, 52, 80, 70].map((v, i) => <div key={i} style={{ flex: 1, height: `${v}%`, background: i === 3 ? accent : '#d4d4d8', borderRadius: 4 }} />)}
              </div>
            </div>
          </div>
        </div>
      );
    case 'folder':
      return (
        <div style={{ display: 'flex', height: '100%' }}>
          <div style={{ width: '20%', background: '#fafafa', borderRight: `1px solid ${T.border}`, padding: 12 }}><Lines n={7} gap={12} /></div>
          <div style={{ flex: 1, padding: 18, display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 16, alignContent: 'start' }}>
            {Array.from({ length: 8 }).map((_, i) => (
              <div key={i} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6 }}>
                <div style={{ width: 54, height: 44, borderRadius: 6, background: i % 3 === 0 ? '#fde68a' : ['#bfdbfe', '#fecaca', '#bbf7d0'][i % 3] }} />
                <Bar w="70%" h={6} />
              </div>
            ))}
          </div>
        </div>
      );
    case 'mail':
      return (
        <div style={{ display: 'flex', height: '100%' }}>
          <div style={{ width: '38%', borderRight: `1px solid ${T.border}` }}>
            {Array.from({ length: 7 }).map((_, i) => (
              <div key={i} style={{ padding: 10, borderBottom: `1px solid ${T.border}`, background: i === 0 ? '#eff6ff' : '#fff' }}>
                <Bar w="45%" h={7} c={i === 0 ? accent : T.fg} /><Bar w="85%" h={6} style={{ marginTop: 6 }} />
              </div>
            ))}
          </div>
          <div style={{ flex: 1, padding: 18, display: 'flex', flexDirection: 'column', gap: 12 }}>
            <Bar w="60%" h={14} c={T.fg} /><Lines n={7} />
          </div>
        </div>
      );
    case 'admin':
    case 'invoice':
    case 'crm':
      return (
        <div style={{ ...pad, display: 'flex', gap: 16 }}>
          <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 12 }}>
            {Array.from({ length: kind === 'crm' ? 4 : 6 }).map((_, i) => (
              <div key={i}>
                <Bar w="25%" h={6} c={T.borderStrong} />
                <div style={{ height: 26, marginTop: 5, border: `1px solid ${T.border}`, borderRadius: 6, display: 'flex', alignItems: 'center', padding: '0 8px' }}>
                  <Bar w={`${35 + i * 9}%`} h={6} />
                </div>
              </div>
            ))}
            <div style={{ alignSelf: 'flex-start', padding: '8px 18px', borderRadius: 8, background: accent, color: '#fff', fontWeight: 700, fontSize: 13 }}>
              {kind === 'invoice' ? '발행하기' : kind === 'admin' ? '상품 등록' : '미팅 추가'}
            </div>
          </div>
          <div style={{ width: '36%', display: 'flex', flexDirection: 'column', gap: 10 }}>
            {kind === 'crm'
              ? ['리드', '제안', '협상'].map((s, i) => (
                  <div key={s} style={{ background: T.muted, borderRadius: 8, padding: 10 }}>
                    <div style={{ fontSize: 11, fontWeight: 700, color: T.subtleFg }}>{s}</div>
                    <Bar w="70%" h={8} c={i === 1 ? accent : '#d4d4d8'} style={{ marginTop: 8 }} />
                  </div>
                ))
              : <div style={{ flex: 1, borderRadius: 10, background: kind === 'admin' ? '#e0e7ff' : '#ffedd5' }} />}
          </div>
        </div>
      );
    case 'browser':
    default:
      return (
        <div style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
          <div style={{ height: 34, background: T.muted, display: 'flex', alignItems: 'center', padding: '0 12px' }}>
            <div style={{ flex: 1, height: 20, borderRadius: 10, background: '#fff', border: `1px solid ${T.border}` }} />
          </div>
          <div style={{ flex: 1, padding: 22, display: 'flex', flexDirection: 'column', gap: 14 }}>
            <div style={{ height: '34%', borderRadius: 10, background: `linear-gradient(120deg, ${accent}, #c4b5fd)` }} />
            <div style={{ display: 'flex', gap: 12 }}>{[0, 1, 2].map((i) => <div key={i} style={{ flex: 1, height: 70, borderRadius: 8, background: T.muted }} />)}</div>
            <Lines n={3} />
          </div>
        </div>
      );
  }
};

export const AppWindow: React.FC<{
  kind: WindowKind;
  title: string;
  w: number;
  h: number;
  style?: React.CSSProperties;
  shadow?: boolean;
}> = ({ kind, title, w, h, style, shadow = true }) => {
  const accent = KIND_COLOR[kind];
  return (
    <div style={{
      width: w, height: h, background: '#fff', borderRadius: 10, overflow: 'hidden',
      border: `1px solid ${T.border}`, fontFamily: FONT,
      boxShadow: shadow ? '0 18px 40px rgba(0,0,0,.18), 0 2px 6px rgba(0,0,0,.08)' : undefined,
      display: 'flex', flexDirection: 'column', ...style,
    }}>
      <div style={{ height: 36, flexShrink: 0, background: '#fafafa', borderBottom: `1px solid ${T.border}`, display: 'flex', alignItems: 'center', padding: '0 12px', gap: 10 }}>
        <div style={{ width: 14, height: 14, borderRadius: 4, background: accent }} />
        <div style={{ fontSize: 14, fontWeight: 600, color: T.fg, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', flex: 1 }}>{title}</div>
        <div style={{ display: 'flex', gap: 18, color: T.mutedFg, fontSize: 13 }}><span>—</span><span>▢</span><span>✕</span></div>
      </div>
      <div style={{ flex: 1, minHeight: 0, position: 'relative' }}>
        <Body kind={kind} accent={accent} />
      </div>
    </div>
  );
};
