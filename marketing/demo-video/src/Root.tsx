import React from 'react';
import { Composition, continueRender, delayRender } from 'remotion';
import { ROLES } from './data/roles';
import { fontsReady } from './fonts';
import { RoleVideo } from './RoleVideo';

const fontHandle = delayRender('Loading Pretendard');
fontsReady.then(() => continueRender(fontHandle));

export const RemotionRoot: React.FC = () => (
  <>
    {ROLES.map((r) => (
      <Composition
        key={r.id}
        id={`demo-${r.id}`}
        component={RoleVideo}
        durationInFrames={600}
        fps={30}
        width={1920}
        height={1080}
        defaultProps={{ roleId: r.id }}
      />
    ))}
  </>
);
