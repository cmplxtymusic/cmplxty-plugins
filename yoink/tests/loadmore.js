'use strict';
// "Load more" regression tests. Run with: node tests/loadmore.js
// The bug: the button vanished when it should not have. A failed YouTube search (rate limit, network blip, bot check)
// used up its step of the search plan, so after a few failures the plan looked finished and the engine reported
// "exhausted" (no more results) even though nothing had been searched. Stop also lost the step and videos in flight.
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { Yoink } = require('../src/core');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-lm-'));
function engine(over) {
  const y = new Yoink({ ytdlp: 'x', ffmpeg: 'x', dataDir: path.join(tmp, 'd' + Math.random().toString(36).slice(2)), clipsDir: path.join(tmp, 'c') });
  y.makeWide = async (v, hit) => ({ file: 'w', winStart: 0, duration: 20, peaks: [] });
  y.renderClip = async (w, a, b, f, out) => out;
  // every video says the phrase once; every query returns 3 fresh videos
  y.getVideoData = async (id) => ({ meta: { title: id, channel: 'c', view_count: 10 }, tracks: [{ words: [{ w: 'hello', t: 1, e: 1.4 }, { w: 'there', t: 1.4, e: 1.8 }], manual: false, lang: 'en' }] });
  let n = 0;
  y.searchVideos = async () => Array.from({ length: 3 }, () => ({ id: ('v' + (++n)).padEnd(11, '_'), duration: 30, channel: 'c', views: 1, title: 't' }));
  return Object.assign(y, over);
}
async function go(y, S, opts, signal) {
  const ev = [];
  await y.search(S, Object.assign({ maxClips: 4, concurrency: 2, retryMs: 1 }, opts), (e) => ev.push(e), signal);
  return { ev, done: ev.filter((e) => e.type === 'done').pop(), clips: ev.filter((e) => e.type === 'clip').length };
}

(async () => {
  // 1. Healthy run: Load more stays available until the plan is really finished, then reports exhausted.
  {
    const y = engine(); const S = y.newSession('hello there', {});
    let rounds = 0, last;
    do { last = await go(y, S); rounds++; assert.ok(rounds < 30, 'never finishes'); if (!last.done.exhausted) assert.ok(last.clips > 0 || S.planIdx < S.plan.length); } while (!last.done.exhausted);
    assert.strictEqual(S.planIdx, S.plan.length); assert.strictEqual(S.queue.length, 0);
    assert.ok(S.shown >= 20, 'expected the whole plan to be used, got ' + S.shown + ' clips');
  }
  // 2. The bug: YouTube refuses every search for a while. The plan must NOT be used up and Load more must stay available.
  {
    const y = engine(); const S = y.newSession('hello there', {});
    const good = y.searchVideos; let failing = true;
    y.searchVideos = async (...a) => { if (failing) throw new Error('HTTP Error 429: Too Many Requests'); return good(...a); };
    for (let i = 0; i < 4; i++) {
      const r = await go(y, S);
      assert.strictEqual(r.done.exhausted, false, 'exhausted after failures (round ' + i + ')');
      assert.strictEqual(r.done.failed, true);
      assert.ok(/Load more/.test(r.done.text), r.done.text);
      assert.ok(S.planIdx < S.plan.length);
    }
    failing = false;
    const r = await go(y, S);
    assert.strictEqual(r.clips, 4, 'recovers once YouTube answers again'); assert.strictEqual(r.done.failed, false);
  }
  // 3. One query that always fails is skipped after a few tries; it must not block the rest forever.
  {
    const y = engine(); const S = y.newSession('hello there', {});
    const good = y.searchVideos; let calls = 0;
    y.searchVideos = async (q, ...a) => { if (q === S.plan[0].q) { calls++; throw new Error('boom'); } return good(q, ...a); };
    let r; for (let i = 0; i < 12 && (!r || r.done.failed || r.clips === 0); i++) r = await go(y, S);
    assert.ok(r.clips > 0, 'moved past the broken query'); assert.ok(calls <= 6, 'broken query tried ' + calls + ' times');
  }
  // 4. Stop in the middle: the interrupted query and the video being checked are not lost.
  {
    const y = engine(); const S = y.newSession('hello there', {});
    const ctl = new AbortController(); const gvd = y.getVideoData; let seen = [];
    y.getVideoData = async (id, f, sig) => { seen.push(id); if (seen.length === 2) { ctl.abort(); throw new Error('aborted'); } return gvd(id); };
    const a = await go(y, S, {}, ctl.signal);
    assert.strictEqual(a.done.stopped, true); assert.strictEqual(a.done.exhausted, false);
    const abortedId = seen[1];
    y.getVideoData = async (id, f, sig) => { seen.push(id); return gvd(id); };
    const b = await go(y, S);
    assert.ok(b.clips > 0);
    assert.ok(seen.filter((id) => id === abortedId).length >= 2, 'the video that was being checked when Stop was pressed is checked again');
  }
  // 5. A stopped run on the very last plan step must not claim "no more results".
  {
    const y = engine(); const S = y.newSession('hello there', {}); S.planIdx = S.plan.length - 1;
    const ctl = new AbortController();
    y.searchVideos = async (q, d, sig) => { ctl.abort(); throw new Error('aborted'); };
    const r = await go(y, S, {}, ctl.signal);
    assert.strictEqual(r.done.exhausted, false); assert.strictEqual(S.planIdx, S.plan.length - 1);
  }
  console.log('loadmore tests passed');
  fs.rmSync(tmp, { recursive: true, force: true });
})().catch((e) => { console.error(e); process.exit(1); });
