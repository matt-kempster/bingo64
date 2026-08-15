import createModule from './dist/bingo64gen.mjs';

const M = await createModule();

const NUM_TYPES = M._gen_num_objective_types();
const VERSION = M.UTF8ToString(M._gen_version());
const OPTIONS = JSON.parse(M.UTF8ToString(M._gen_options_json()));
const OPTION_LABELS = new Map(OPTIONS.map((opt) => [opt.type, opt.label]));

// 0xFA is the in-game filled-star glyph; titles carry it as U+00FA.
const STAR_BYTE = 'ú';
const pretty = (s) => s.replaceAll(STAR_BYTE, '★');

const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------------------
// State: seed, target, set of disabled objective types. Kept in the URL hash
// so a board can be shared as a link.

const state = {
  seed: null,
  target: 1,
  off: new Set(),
};

function readHash() {
  const params = new URLSearchParams(location.hash.slice(1));
  const s = params.get('s');
  state.seed = s !== null && /^\d{1,9}$/.test(s) ? Number(s) : randomSeed();
  const t = Number(params.get('t'));
  state.target = [1, 2, 3, 12].includes(t) ? t : 1;
  state.off = new Set(
    (params.get('off') ?? '')
      .split(',')
      .filter((part) => part !== '') // ''.split(',') is [''], and Number('') is 0
      .map(Number)
      .filter((n) => Number.isInteger(n) && n >= 0 && n < NUM_TYPES),
  );
}

function writeHash() {
  const params = new URLSearchParams();
  params.set('s', String(state.seed));
  if (state.target !== 1) params.set('t', String(state.target));
  if (state.off.size > 0) params.set('off', [...state.off].sort((a, b) => a - b).join(','));
  history.replaceState(null, '', '#' + params.toString());
}

function randomSeed() {
  return Math.floor(Math.random() * 1000000000);
}

// ---------------------------------------------------------------------------
// Generator calls.

const disabledPtr = M._malloc(NUM_TYPES);

function generateBoard() {
  M.HEAPU8.fill(0, disabledPtr, disabledPtr + NUM_TYPES);
  for (const t of state.off) M.HEAPU8[disabledPtr + t] = 1;
  const ptr = M._gen_board_json(state.seed, state.target, disabledPtr);
  const board = JSON.parse(M.UTF8ToString(ptr));
  // A fresh board's "Remaining: N" always repeats the count already stated
  // in the sentence; drop it everywhere.
  for (const cell of board.cells) {
    cell.desc = cell.desc.replace(/\. Remaining: \d+/, '');
  }
  return board;
}

// Icon textures are 16x16 RGBA16 (5/5/5/1, big-endian), straight from the
// compiled game data.
const iconCache = new Map();

function iconImageData(icon) {
  if (iconCache.has(icon)) return iconCache.get(icon);
  const ptr = M._gen_icon_rgba16(icon);
  let img = null;
  if (ptr !== 0) {
    const bytes = M.HEAPU8.subarray(ptr, ptr + 16 * 16 * 2);
    img = new ImageData(16, 16);
    for (let i = 0; i < 256; i++) {
      const px = (bytes[i * 2] << 8) | bytes[i * 2 + 1];
      img.data[i * 4 + 0] = ((px >> 11) & 0x1f) * 255 / 31;
      img.data[i * 4 + 1] = ((px >> 6) & 0x1f) * 255 / 31;
      img.data[i * 4 + 2] = ((px >> 1) & 0x1f) * 255 / 31;
      img.data[i * 4 + 3] = (px & 1) * 255;
    }
  }
  iconCache.set(icon, img);
  return img;
}

function iconCanvas(icon) {
  const canvas = document.createElement('canvas');
  canvas.width = 16;
  canvas.height = 16;
  canvas.className = 'icon';
  const img = iconImageData(icon);
  if (img) canvas.getContext('2d').putImageData(img, 0, 0);
  return canvas;
}

// ---------------------------------------------------------------------------
// Rendering.

let currentBoard = null;
let selectedCell = -1;

const coordOf = (i) => 'ABCDE'[i % 5] + (Math.floor(i / 5) + 1);

function showCellDetail(i) {
  const panel = $('cellDetail');
  panel.replaceChildren();
  if (i < 0) {
    panel.appendChild(el('p', 'detail-hint', 'Click a cell to see its objective here.'));
    return;
  }
  const cell = currentBoard.cells[i];
  const head = el('div', 'detail-head');
  head.appendChild(iconCanvas(cell.icon));
  const heading = el('div');
  heading.appendChild(el('div', 'detail-coord', coordOf(i)));
  heading.appendChild(el('div', 'detail-title', pretty(cell.title)));
  head.appendChild(heading);
  panel.appendChild(head);
  panel.appendChild(el('p', 'detail-desc', cell.desc));
  panel.appendChild(el('p', 'detail-kind', OPTION_LABELS.get(cell.type) ?? ''));
}

function selectCell(i) {
  selectedCell = selectedCell === i ? -1 : i;
  const cells = $('board').querySelectorAll('.cell');
  cells.forEach((node, idx) => node.classList.toggle('selected', idx === selectedCell));
  showCellDetail(selectedCell);
}

function render() {
  currentBoard = generateBoard();
  writeHash();
  selectedCell = -1;
  showCellDetail(-1);

  $('seed').value = String(state.seed);
  $('target').value = String(state.target);

  const board = $('board');
  board.replaceChildren();
  const cols = 'ABCDE';
  // Header row: corner + column letters.
  board.appendChild(el('div', 'coord'));
  for (let j = 0; j < 5; j++) board.appendChild(el('div', 'coord', cols[j]));
  currentBoard.cells.forEach((cell, i) => {
    if (i % 5 === 0) board.appendChild(el('div', 'coord', String(i / 5 + 1)));
    const div = el('div', 'cell');
    div.title = `${cols[i % 5]}${Math.floor(i / 5) + 1}: ${cell.desc}`;
    div.appendChild(iconCanvas(cell.icon));
    div.appendChild(el('div', 'cell-title', pretty(cell.title)));
    div.addEventListener('click', () => selectCell(i));
    board.appendChild(div);
  });

  const descs = $('descriptions');
  descs.replaceChildren();
  currentBoard.cells.forEach((cell, i) => {
    const li = el('li');
    li.appendChild(el('span', 'coord-label', `${cols[i % 5]}${Math.floor(i / 5) + 1}`));
    li.appendChild(el('span', null, cell.desc));
    descs.appendChild(li);
  });

  const offCount = state.off.size;
  $('optionsSummary').textContent = offCount > 0 ? `(${offCount} turned off)` : '';
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function buildOptionsList() {
  const list = $('optionsList');
  list.replaceChildren();
  for (const opt of OPTIONS) {
    const label = el('label', 'option');
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = !state.off.has(opt.type);
    box.addEventListener('change', () => {
      if (box.checked) state.off.delete(opt.type);
      else state.off.add(opt.type);
      render();
    });
    label.appendChild(box);
    label.appendChild(iconCanvas(opt.icon));
    label.appendChild(el('span', null, opt.label));
    list.appendChild(label);
  }
}

function syncOptionCheckboxes() {
  const boxes = $('optionsList').querySelectorAll('input');
  OPTIONS.forEach((opt, i) => {
    boxes[i].checked = !state.off.has(opt.type);
  });
}

// ---------------------------------------------------------------------------
// Clipboard helpers.

function toast(message) {
  const node = $('toast');
  node.textContent = message;
  node.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { node.hidden = true; }, 1800);
}

async function copyText(text, message) {
  try {
    await navigator.clipboard.writeText(text);
    toast(message);
  } catch {
    toast('Copy failed - is the page served over http(s)?');
  }
}

function bingosyncGoals() {
  const useTitle = $('goalStyle').value === 'title';
  return JSON.stringify(
    currentBoard.cells.map((cell) => {
      let name;
      if (useTitle) {
        // The bare in-game title ("x6") means nothing off-screen; pair it
        // with the objective's name from the options menu.
        name = `${OPTION_LABELS.get(cell.type) ?? '?'}: ${pretty(cell.title)}`;
      } else {
        name = cell.desc;
      }
      return { name };
    }),
  );
}

// ---------------------------------------------------------------------------
// Wiring.

$('random').addEventListener('click', () => {
  state.seed = randomSeed();
  render();
});

$('seed').addEventListener('change', () => {
  const raw = $('seed').value.trim();
  if (/^\d{1,9}$/.test(raw)) {
    state.seed = Number(raw);
    render();
  } else {
    toast('Seed must be 1-9 digits');
    $('seed').value = String(state.seed);
  }
});

$('target').addEventListener('change', () => {
  state.target = Number($('target').value);
  render();
});

$('toggleAll').addEventListener('click', () => {
  if (state.off.size < NUM_TYPES) {
    for (const opt of OPTIONS) state.off.add(opt.type);
  } else {
    state.off.clear();
  }
  syncOptionCheckboxes();
  render();
});

$('permalink').addEventListener('click', () => {
  copyText(location.href, 'Link copied');
});

$('copyGoals').addEventListener('click', () => {
  copyText(bingosyncGoals(), 'Bingosync goals copied');
});

window.addEventListener('hashchange', () => {
  readHash();
  syncOptionCheckboxes();
  render();
});

$('version').textContent = VERSION;
readHash();
buildOptionsList();
render();
