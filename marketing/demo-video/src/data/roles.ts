// Use cases per role (see marketing/usecases.md). Windows are generic look-alikes, never real brands.
import { PILE } from '../theme';

export type WindowKind =
  | 'board' | 'design' | 'doc' | 'chat' | 'dashboard' | 'log' | 'sheet' | 'slides' | 'ads'
  | 'folder' | 'pdf' | 'mail' | 'admin' | 'crm' | 'code' | 'terminal' | 'browser' | 'pr'
  | 'erp' | 'invoice' | 'moodboard';

export interface WinSpec {
  kind: WindowKind;
  title: string;
}

export interface PileSpec {
  emoji: string;
  name: string;
  color: string;
  windows: WinSpec[];
}

export interface Role {
  id: string;
  label: string;   // 직무
  person: string;  // 이름
  when: string;    // 요일·시각
  piles: [PileSpec, PileSpec, PileSpec];  // [0] 하던 일, [1] 끼어드는 일, [2] 대기 중인 일
  interrupt: { from: string; text: string; icon: string };
  problems: [string, string, string];  // 최소화 / 가상 데스크톱 / 다 띄움
  openCount: number;  // hook counter
}

export const ROLES: Role[] = [
  {
    id: 'pm',
    label: 'PM',
    person: '지은',
    when: '화요일 오전 10:42',
    piles: [
      { emoji: '🗂', name: '3월 스프린트 기획', color: PILE.blue, windows: [
        { kind: 'board', title: '이슈 보드 — 3월 스프린트' },
        { kind: 'design', title: '화면 설계 — 결제 개편' },
        { kind: 'doc', title: '기획서_결제개편_v3' },
      ] },
      { emoji: '🔥', name: '결제 오류 대응', color: PILE.red, windows: [
        { kind: 'chat', title: '메신저 — #장애대응' },
        { kind: 'dashboard', title: '모니터링 — 결제 실패율' },
        { kind: 'log', title: '서버 로그 — payment-api' },
      ] },
      { emoji: '📊', name: '주간 보고', color: PILE.green, windows: [
        { kind: 'sheet', title: '주간 지표.xlsx' },
        { kind: 'slides', title: '주간 보고.pptx' },
      ] },
    ],
    interrupt: { from: '#장애대응', text: '🚨 결제 실패율 12% 급증, 확인 부탁드려요', icon: '💬' },
    problems: ['기획서 어디 뒀더라?', '장애 채널이 데스크톱 3에…', '창 23개, 탭 61개'],
    openCount: 23,
  },
  {
    id: 'marketer',
    label: '마케터',
    person: '민호',
    when: '수요일 오후 2:15',
    piles: [
      { emoji: '🎯', name: '봄 시즌 캠페인', color: PILE.orange, windows: [
        { kind: 'ads', title: '광고 관리자 — 봄 캠페인' },
        { kind: 'dashboard', title: '분석 — 전환 퍼널' },
        { kind: 'folder', title: '소재_봄시즌' },
      ] },
      { emoji: '🤝', name: '인플루언서 협업', color: PILE.pink, windows: [
        { kind: 'chat', title: '메신저 — 인플루언서 DM' },
        { kind: 'pdf', title: '협업 계약서.pdf' },
        { kind: 'sheet', title: '섭외 리스트.xlsx' },
      ] },
      { emoji: '📈', name: '월간 리포트', color: PILE.purple, windows: [
        { kind: 'sheet', title: '3월 성과.xlsx' },
        { kind: 'slides', title: '월간 리포트.pptx' },
      ] },
    ],
    interrupt: { from: '인플루언서 @daily.lia', text: '내일 업로드 전에 원고 확인 부탁드려요!', icon: '💬' },
    problems: ['ROAS 보던 창이 어디…', '계약서 열어둔 거 까먹음', '대시보드만 7개'],
    openCount: 19,
  },
  {
    id: 'md',
    label: 'MD',
    person: '수빈',
    when: '목요일 오전 9:30',
    piles: [
      { emoji: '🛍', name: '신상 기획전 오픈', color: PILE.blue, windows: [
        { kind: 'admin', title: '상품 등록 — 어드민' },
        { kind: 'design', title: '상세페이지 시안' },
        { kind: 'sheet', title: '기획전 가격표.xlsx' },
      ] },
      { emoji: '📦', name: '재고 부족 대응', color: PILE.red, windows: [
        { kind: 'sheet', title: '재고 현황.xlsx' },
        { kind: 'mail', title: '메일 — 협력사' },
        { kind: 'doc', title: '긴급 발주서' },
      ] },
      { emoji: '🏷', name: '프로모션 정산', color: PILE.green, windows: [
        { kind: 'sheet', title: '프로모션 정산.xlsx' },
        { kind: 'dashboard', title: '쿠폰 사용 현황' },
      ] },
    ],
    interrupt: { from: '협력사 메일', text: '베스트 상품 B-102, 이번 주 품절 예정입니다', icon: '✉️' },
    problems: ['가격표 수정하다 말았는데…', '발주서는 어느 데스크톱?', '엑셀만 9개'],
    openCount: 21,
  },
  {
    id: 'sales',
    label: '영업',
    person: '태준',
    when: '월요일 오후 1:50',
    piles: [
      { emoji: '💰', name: 'A사 견적', color: PILE.green, windows: [
        { kind: 'sheet', title: 'A사 견적서.xlsx' },
        { kind: 'pdf', title: '2026 단가표.pdf' },
        { kind: 'mail', title: '메일 — A사 구매팀' },
      ] },
      { emoji: '📞', name: 'B사 긴급 미팅', color: PILE.orange, windows: [
        { kind: 'crm', title: '고객 관리 — B사' },
        { kind: 'slides', title: 'B사 제안서.pptx' },
        { kind: 'doc', title: 'B사 회의록' },
      ] },
      { emoji: '📝', name: 'C사 계약 검토', color: PILE.gray, windows: [
        { kind: 'doc', title: 'C사 계약서_수정본' },
        { kind: 'mail', title: '메일 — 법무팀' },
      ] },
    ],
    interrupt: { from: 'B사 김부장', text: '오늘 3시로 미팅 당겨도 될까요?', icon: '💬' },
    problems: ['견적 단가 어디까지 넣었지?', '제안서는 데스크톱 2에…', '메일 창만 6개'],
    openCount: 17,
  },
  {
    id: 'designer',
    label: '디자이너',
    person: '하린',
    when: '금요일 오전 11:05',
    piles: [
      { emoji: '🎨', name: '홈 리뉴얼 시안', color: PILE.purple, windows: [
        { kind: 'design', title: '홈 리뉴얼 — 디자인' },
        { kind: 'folder', title: '레퍼런스' },
        { kind: 'moodboard', title: '무드보드' },
      ] },
      { emoji: '✏️', name: 'QA 수정 요청', color: PILE.pink, windows: [
        { kind: 'chat', title: '메신저 — #qa' },
        { kind: 'board', title: '이슈 보드 — QA' },
        { kind: 'browser', title: '스테이징 — 홈' },
      ] },
      { emoji: '📦', name: '에셋 전달', color: PILE.blue, windows: [
        { kind: 'folder', title: 'export_2x' },
        { kind: 'folder', title: '공유 드라이브' },
      ] },
    ],
    interrupt: { from: '#qa · 개발팀', text: '버튼 간격이 시안이랑 달라요, 확인 부탁!', icon: '💬' },
    problems: ['레퍼런스 폴더 어디 갔지?', '무드보드를 잊고 있었다', '아트보드보다 창이 더 많음'],
    openCount: 18,
  },
  {
    id: 'developer',
    label: '개발자',
    person: '도윤',
    when: '화요일 오후 4:20',
    piles: [
      { emoji: '💻', name: '검색 기능 개발', color: PILE.blue, windows: [
        { kind: 'code', title: 'search.ts — 코드 에디터' },
        { kind: 'terminal', title: '터미널 — dev server' },
        { kind: 'browser', title: 'localhost:3000/search' },
      ] },
      { emoji: '🚨', name: '운영 핫픽스', color: PILE.red, windows: [
        { kind: 'dashboard', title: '에러 모니터링' },
        { kind: 'log', title: '로그 — payment-api' },
        { kind: 'code', title: 'PaymentService.java' },
      ] },
      { emoji: '👀', name: '코드 리뷰', color: PILE.gray, windows: [
        { kind: 'pr', title: 'PR #482 — 장바구니 리팩터링' },
        { kind: 'pr', title: 'PR #485 — 쿠폰 API' },
      ] },
    ],
    interrupt: { from: '에러 알림', text: '🔴 에러 342건: NullPointerException (결제 API)', icon: '🔔' },
    problems: ['터미널 어느 창이었더라?', '리뷰 요청 3일째 방치', '터미널 탭 14개'],
    openCount: 26,
  },
  {
    id: 'finance',
    label: '회계/재무',
    person: '서연',
    when: '월말 오후 3:40',
    piles: [
      { emoji: '🧾', name: '3월 결산', color: PILE.green, windows: [
        { kind: 'erp', title: '회계 시스템 — 결산' },
        { kind: 'sheet', title: '3월 결산.xlsx' },
        { kind: 'folder', title: '증빙_3월' },
      ] },
      { emoji: '⚡', name: '세금계산서 발행', color: PILE.orange, windows: [
        { kind: 'invoice', title: '세금계산서 발행' },
        { kind: 'mail', title: '메일 — 영업팀' },
      ] },
      { emoji: '💳', name: '경비 정산', color: PILE.purple, windows: [
        { kind: 'folder', title: '영수증_3월' },
        { kind: 'sheet', title: '경비 정산.xlsx' },
      ] },
    ],
    interrupt: { from: '영업팀 태준', text: '오늘 중으로 세금계산서 발행 부탁드려요!', icon: '💬' },
    problems: ['결산 시트 몇 행까지 했지?', '증빙 폴더가 안 보여요', '숫자가 다 숫자로 보임'],
    openCount: 20,
  },
];

export const roleById = (id: string) => ROLES.find((r) => r.id === id) ?? ROLES[0];
