function test_seek13(v, seekTime, is, ok, finish) {
var completed = false;

function startTest() {
  if (completed)
    return;
  ok(!v.seeking, "seeking should default to false");
  v.currentTime = v.duration;
  is(v.currentTime, v.duration, "currentTime must report seek target immediately");
  is(v.seeking, true, "seeking flag on start should be true");
}

function seekStarted() {
  if (completed)
    return;
  //is(v.currentTime, v.duration, "seeking: currentTime must be duration");
  ok(Math.abs(v.currentTime - v.duration) < 0.01, "seeking: currentTime must be duration");
}

function seekEnded() {
  if (completed)
    return;
  //is(v.currentTime, v.duration, "seeked: currentTime must be duration");
  ok(Math.abs(v.currentTime - v.duration) < 0.01, "seeked: currentTime must be duration");
  is(v.seeking, false, "seeking flag on end should be false");
}

function playbackEnded() {
  if (completed)
    return;
  completed = true;
  //is(v.currentTime, v.duration, "ended: currentTime must be duration");
  ok(Math.abs(v.currentTime - v.duration) < 0.01, "ended: currentTime must be duration");
  is(v.seeking, false, "seeking flag on end should be false");
  is(v.ended, true, "ended must be true");
  finish();
}

v.addEventListener("loadedmetadata", startTest, false);
v.addEventListener("seeking", seekStarted, false);
v.addEventListener("seeked", seekEnded, false);
v.addEventListener("ended", playbackEnded, false);
}
