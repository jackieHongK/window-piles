// Pretendard (SIL OFL 1.1): free for commercial use.
import { loadFont } from '@remotion/fonts';
import { staticFile } from 'remotion';

const weights: [string, string][] = [
  ['Regular', '400'],
  ['SemiBold', '600'],
  ['Bold', '700'],
  ['ExtraBold', '800'],
  ['Black', '900'],
];

export const fontsReady = Promise.all(
  weights.map(([name, weight]) =>
    loadFont({ family: 'Pretendard', url: staticFile(`fonts/Pretendard-${name}.woff2`), weight }),
  ),
);
