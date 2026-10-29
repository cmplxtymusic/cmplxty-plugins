const assert = require('assert');
const { parseJson3, findPhrase, normTokens, normFilters, passesCheap, passesMeta, parseYM, joinWords } = require('../src/core');
assert.deepStrictEqual(normTokens("Don't STOP, now!"), ["don't", 'stop', 'now']);
const auto = parseJson3(JSON.stringify({ events: [{ tStartMs: 1000, dDurationMs: 3000, segs: [{ utf8: 'well' }, { utf8: ' what', tOffsetMs: 300 }, { utf8: ' the', tOffsetMs: 500 }, { utf8: ' heck', tOffsetMs: 700 }, { utf8: ' man', tOffsetMs: 1200 }] }] }));
let h = findPhrase(auto, 'What the heck');
assert.strictEqual(h.length, 1); assert.ok(Math.abs(h[0].start - 1.3) < 1e-6); assert.ok(h[0].end > 1.7 && h[0].end <= 2.2); assert.strictEqual(h[0].approx, false);
assert.deepStrictEqual(h[0].words.slice(h[0].hiFrom, h[0].hiTo + 1), ['what', 'the', 'heck']);
assert.strictEqual(findPhrase(auto, 'the hec').length, 0);            // no partial words
assert.strictEqual(findPhrase(auto, 'heck man now').length, 0);
const manual = parseJson3(JSON.stringify({ events: [{ tStartMs: 5000, dDurationMs: 2000, segs: [{ utf8: 'Doggone it, pimp!' }] }] }));
h = findPhrase(manual, 'dog gone it pimp'); assert.strictEqual(h.length, 1); assert.strictEqual(h[0].approx, true);
// Unicode: accents, Cyrillic, Greek, CJK (one token per character)
assert.deepStrictEqual(normTokens('¿Qué pasó, señor?'), ['qué', 'pasó', 'señor']);
assert.deepStrictEqual(normTokens('Привет, мир!'), ['привет', 'мир']);
assert.deepStrictEqual(normTokens('你好世界'), ['你', '好', '世', '界']);
const es = parseJson3(JSON.stringify({ events: [{ tStartMs: 0, dDurationMs: 2000, segs: [{ utf8: 'Hola, ¿qué pasó señor López?' }] }] }));
assert.strictEqual(findPhrase(es, 'qué pasó').length, 1);
assert.strictEqual(findPhrase(es, 'QUE PASO').length, 0); // accents are not stripped, "que" is a different word than "qué"
const ru = parseJson3(JSON.stringify({ events: [{ tStartMs: 0, dDurationMs: 2000, segs: [{ utf8: 'Ну привет мир как дела' }] }] }));
assert.strictEqual(findPhrase(ru, 'Привет мир').length, 1);
const zh = parseJson3(JSON.stringify({ events: [{ tStartMs: 0, dDurationMs: 4000, segs: [{ utf8: '大家好今天我们来说世界和平' }] }] }));
h = findPhrase(zh, '世界'); assert.strictEqual(h.length, 1); assert.strictEqual(joinWords(h[0].words), h[0].line); assert.ok(!/ /.test(h[0].line));
// Filters
assert.deepStrictEqual(parseYM('2020/05'), { y: 2020, m: 5 }); assert.strictEqual(parseYM('20/5'), null); assert.deepStrictEqual(parseYM('2019'), { y: 2019, m: null });
const F = normFilters({ cc: true, language: 'ES', category: 'Music', region: 'jp', channel: ' vevo ', title: 'live', viewsMin: '1,000', viewsMax: '', uploadFrom: '2020/01', uploadTo: '2021/06' });
assert.strictEqual(F.language, 'es'); assert.strictEqual(F.region, 'JP'); assert.strictEqual(F.viewsMin, 1000); assert.strictEqual(F.viewsMax, null); assert.strictEqual(F.channel, 'vevo');
assert.strictEqual(normFilters({ category: 'bogus' }).category, 'all');
assert.strictEqual(passesCheap({ title: 'Live at Wembley', channel: 'Queen VEVO', views: 5000 }, F), true);
assert.strictEqual(passesCheap({ title: 'Live at Wembley', channel: 'Queen VEVO', views: 500 }, F), false);
assert.strictEqual(passesCheap({ title: 'Studio', channel: 'Queen VEVO', views: 5000 }, F), false);
assert.strictEqual(passesCheap({ title: 'Live', channel: 'Other', views: 5000 }, F), false);
assert.strictEqual(passesCheap({ title: 'Live', channel: 'VEVO', views: null }, F), true); // unknown views: decided later with full metadata
const meta = { license: 'Creative Commons Attribution license (reuse allowed)', categories: ['Music'], channel: 'Queen VEVO', title: 'Live', view_count: 5000, upload_date: '20200315', subKeys: ['es'] };
assert.strictEqual(passesMeta(meta, F), true);
assert.strictEqual(passesMeta({ ...meta, license: 'Standard YouTube License' }, F), false);
assert.strictEqual(passesMeta({ ...meta, categories: ['Comedy'] }, F), false);
assert.strictEqual(passesMeta({ ...meta, upload_date: '20210701' }, F), false);
assert.strictEqual(passesMeta({ ...meta, upload_date: '20210630' }, F), true);
assert.strictEqual(passesMeta({ ...meta, upload_date: '20191231' }, F), false);
assert.strictEqual(passesMeta({ ...meta, subKeys: [] }, normFilters({ manual: true })), false);
console.log('unit tests ok');
