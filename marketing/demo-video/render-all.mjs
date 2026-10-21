// Renders every role's draft: node render-all.mjs [--stills] [--only pm,md]
//   videos → out/<role>.mp4      stills (review frames) → out/stills/<role>-<frame>.png
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { bundle } from '@remotion/bundler';
import { renderMedia, renderStill, selectComposition } from '@remotion/renderer';

const root = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const stills = args.includes('--stills');
const onlyIdx = args.indexOf('--only');
const only = onlyIdx >= 0 ? args[onlyIdx + 1].split(',') : null;
const ROLE_IDS = ['pm', 'marketer', 'md', 'sales', 'designer', 'developer', 'finance'].filter((r) => !only || only.includes(r));
const STILL_FRAMES = [70, 115, 165, 215, 280, 345, 400, 450, 480, 570];

console.log('Bundling…');
const serveUrl = await bundle({ entryPoint: path.join(root, 'src', 'index.ts'), publicDir: path.join(root, 'public') });

for (const id of ROLE_IDS) {
  const composition = await selectComposition({ serveUrl, id: `demo-${id}`, inputProps: { roleId: id } });
  if (stills) {
    for (const frame of STILL_FRAMES) {
      const output = path.join(root, 'out', 'stills', `${id}-${String(frame).padStart(3, '0')}.png`);
      await renderStill({ composition, serveUrl, output, frame, inputProps: { roleId: id } });
    }
    console.log(`stills: ${id}`);
  } else {
    const output = path.join(root, 'out', `${id}.mp4`);
    const t0 = Date.now();
    await renderMedia({
      composition, serveUrl, codec: 'h264', outputLocation: output, inputProps: { roleId: id },
      crf: 18, concurrency: null,
    });
    console.log(`video: ${output} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
  }
}
